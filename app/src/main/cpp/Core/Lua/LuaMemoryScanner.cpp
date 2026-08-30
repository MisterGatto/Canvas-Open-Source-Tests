#include "LuaMemoryScanner.h"
#include <fstream>
#include <sstream>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <android/log.h>

#define LOG_TAG "CanvasLuaScanner"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace Canvas::Lua {

static bool isMemoryResident(uintptr_t address, size_t size) {
    if (!address || !size) return false;
    constexpr uintptr_t PAGE_SZ = 4096;
    uintptr_t pageStart = address & ~(PAGE_SZ - 1);
    uintptr_t pageEnd = (address + size + PAGE_SZ - 1) & ~(PAGE_SZ - 1);
    size_t pageCount = (pageEnd - pageStart) / PAGE_SZ;

    if (pageCount == 0 || pageCount > 65536) return false;

    std::vector<unsigned char> vec(pageCount);
    if (mincore(reinterpret_cast<void*>(pageStart), pageCount * PAGE_SZ, vec.data()) != 0) {
        return false;
    }

    for (size_t i = 0; i < pageCount; ++i) {
        if (!(vec[i] & 1)) {
            return false;
        }
    }
    return true;
}

LuaMemoryScanner::LuaMemoryScanner() {
}

void LuaMemoryScanner::setTargetRegions(uint32_t regionsMask) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_targetRegions = regionsMask;
}

uint32_t LuaMemoryScanner::getTargetRegions() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_targetRegions;
}

uint32_t LuaMemoryScanner::classifyRegion(const std::string& path, int perms) {
    bool isShared = (perms & 0x1000); // custom flag for shared mappings
    if (isShared || path.rfind("/dev/", 0) == 0) {
        return GG_REGION_OTHER;
    }
    if (path.find("guard") != std::string::npos || path.find("quarantine") != std::string::npos ||
        path.find("linker") != std::string::npos || path.find("bionic") != std::string::npos) {
        return GG_REGION_OTHER;
    }
    // Filter out emulator binary translation bridges (Houdini, NDK translation, Berberis, QEMU, VBox)
    if (path.find("houdini") != std::string::npos || path.find("ndk_translation") != std::string::npos ||
        path.find("berberis") != std::string::npos || path.find("goldfish") != std::string::npos ||
        path.find("qemu") != std::string::npos || path.find("vbox") != std::string::npos ||
        path.find("nemu") != std::string::npos || path.find("ldplayer") != std::string::npos ||
        path.find("bluestacks") != std::string::npos || path.find("bignox") != std::string::npos ||
        path.find("genymotion") != std::string::npos || path.find("host_") != std::string::npos) {
        return GG_REGION_OTHER;
    }
    if (path.find("scudo") != std::string::npos || path.find("malloc") != std::string::npos ||
        path.find("GWP-ASan") != std::string::npos) {
        return GG_REGION_C_ALLOC;
    }
    if (path.empty() || path == "[anon]" || path.find("[anon:") != std::string::npos) {
        return GG_REGION_ANONYMOUS | GG_REGION_C_ALLOC;
    }
    if (path.find("[stack") != std::string::npos) {
        return GG_REGION_STACK;
    }
    if (path.find("dalvik") != std::string::npos || path.find("boot.art") != std::string::npos ||
        path.find(".art") != std::string::npos || path.find(".oat") != std::string::npos) {
        return GG_REGION_JAVA_HEAP | GG_REGION_JAVA;
    }
    if (path.find(".so") != std::string::npos) {
        bool isApp = (path.find("libBootloader.so") != std::string::npos ||
                      path.find("libSky.so") != std::string::npos ||
                      path.find("libciphered.so") != std::string::npos ||
                      path.find("/data/app/") != std::string::npos ||
                      path.find("/data/data/") != std::string::npos);
        if (perms & PROT_EXEC) {
            return isApp ? GG_REGION_CODE_APP : GG_REGION_CODE_SYS;
        } else if (perms & PROT_WRITE) {
            return GG_REGION_C_BSS | GG_REGION_C_DATA;
        } else {
            return GG_REGION_C_DATA;
        }
    }
    return GG_REGION_OTHER;
}

void LuaMemoryScanner::refreshRanges(std::vector<MemoryRange>& ranges) {
    ranges.clear();
    std::ifstream mapsFile("/proc/self/maps");
    if (!mapsFile.is_open()) return;

    std::string line;
    while (std::getline(mapsFile, line)) {
        if (line.empty()) continue;

        size_t dash = line.find('-');
        if (dash == std::string::npos) continue;

        size_t space1 = line.find(' ', dash);
        if (space1 == std::string::npos) continue;

        size_t space2 = line.find(' ', space1 + 1);
        if (space2 == std::string::npos) continue;

        std::string startStr = line.substr(0, dash);
        std::string endStr = line.substr(dash + 1, space1 - dash - 1);
        std::string permsStr = line.substr(space1 + 1, space2 - space1 - 1);

        uintptr_t start = 0, end = 0;
        try {
            start = std::stoull(startStr, nullptr, 16);
            end = std::stoull(endStr, nullptr, 16);
        } catch (...) {
            continue;
        }

        int prot = 0;
        if (permsStr.find('r') != std::string::npos) prot |= PROT_READ;
        if (permsStr.find('w') != std::string::npos) prot |= PROT_WRITE;
        if (permsStr.find('x') != std::string::npos) prot |= PROT_EXEC;
        if (permsStr.find('s') != std::string::npos) prot |= 0x1000; // mark shared

        if (!(prot & PROT_READ)) continue;

        std::string pathStr;
        size_t curPos = space2;
        int spacesFound = 0;
        while (curPos < line.size() && spacesFound < 3) {
            if (line[curPos] == ' ' && (curPos + 1 < line.size() && line[curPos + 1] != ' ')) {
                spacesFound++;
            }
            curPos++;
        }
        if (curPos < line.size()) {
            pathStr = line.substr(curPos);
            while (!pathStr.empty() && (pathStr.front() == ' ' || pathStr.front() == '\t')) pathStr.erase(pathStr.begin());
            while (!pathStr.empty() && (pathStr.back() == ' ' || pathStr.back() == '\r' || pathStr.back() == '\n')) pathStr.pop_back();
        }

        MemoryRange range;
        range.start = start;
        range.end = end;
        range.protection = (prot & 0xFF);
        range.perms = permsStr;
        range.path = pathStr;
        range.name = pathStr.empty() ? "[anon]" : pathStr.substr(pathStr.find_last_of('/') + 1);
        range.regionType = classifyRegion(pathStr, prot);

        ranges.push_back(range);
    }
}

std::vector<MemoryRange> LuaMemoryScanner::getMemoryRanges(const std::string& filter) {
    std::vector<MemoryRange> allRanges;
    refreshRanges(allRanges);

    if (filter.empty()) return allRanges;

    std::string lowerFilter = filter;
    std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::tolower);

    std::vector<MemoryRange> filtered;
    for (const auto& r : allRanges) {
        std::string lowerName = r.name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
        std::string lowerPath = r.path;
        std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);

        if (lowerName.find(lowerFilter) != std::string::npos || lowerPath.find(lowerFilter) != std::string::npos) {
            filtered.push_back(r);
        }
    }
    return filtered;
}

bool LuaMemoryScanner::parseQuery(const std::string& query, uint32_t type, double& outVal1, double& outVal2, bool& isRange) {
    isRange = false;
    size_t tildePos = query.find('~');
    if (tildePos != std::string::npos) {
        isRange = true;
        try {
            outVal1 = std::stod(query.substr(0, tildePos));
            outVal2 = std::stod(query.substr(tildePos + 1));
            if (outVal1 > outVal2) std::swap(outVal1, outVal2);
            return true;
        } catch (...) {
            return false;
        }
    }

    try {
        if (query.rfind("0x", 0) == 0 || query.rfind("0X", 0) == 0) {
            outVal1 = static_cast<double>(std::stoull(query, nullptr, 16));
        } else {
            outVal1 = std::stod(query);
        }
        outVal2 = outVal1;
        return true;
    } catch (...) {
        return false;
    }
}

static bool matchBufferValue(const uint8_t* ptr, uint32_t type, double qVal1, double qVal2, bool isRange, uint32_t sign, std::string* outVal = nullptr) {
    double memVal = 0.0;
    switch (type) {
        case GG_TYPE_BYTE: {
            uint8_t v = *ptr;
            memVal = v;
            if (outVal) *outVal = std::to_string(v);
            break;
        }
        case GG_TYPE_WORD: {
            int16_t v = 0;
            std::memcpy(&v, ptr, 2);
            memVal = v;
            if (outVal) *outVal = std::to_string(v);
            break;
        }
        case GG_TYPE_DWORD:
        case GG_TYPE_AUTO: {
            int32_t v = 0;
            std::memcpy(&v, ptr, 4);
            memVal = v;
            if (outVal) *outVal = std::to_string(v);
            break;
        }
        case GG_TYPE_QWORD: {
            int64_t v = 0;
            std::memcpy(&v, ptr, 8);
            memVal = static_cast<double>(v);
            if (outVal) *outVal = std::to_string(v);
            break;
        }
        case GG_TYPE_FLOAT: {
            float v = 0.0f;
            std::memcpy(&v, ptr, 4);
            memVal = v;
            if (outVal) *outVal = std::to_string(v);
            break;
        }
        case GG_TYPE_DOUBLE: {
            double v = 0.0;
            std::memcpy(&v, ptr, 8);
            memVal = v;
            if (outVal) *outVal = std::to_string(v);
            break;
        }
        default:
            return false;
    }

    if (isRange) {
        return (memVal >= qVal1 && memVal <= qVal2);
    }

    constexpr double EPSILON = 0.0001;
    if (sign & GG_SIGN_EQUAL) {
        if (type == GG_TYPE_FLOAT || type == GG_TYPE_DOUBLE) {
            return std::fabs(memVal - qVal1) < EPSILON;
        }
        return static_cast<int64_t>(memVal) == static_cast<int64_t>(qVal1);
    }
    if (sign & GG_SIGN_NOT_EQUAL) {
        return static_cast<int64_t>(memVal) != static_cast<int64_t>(qVal1);
    }
    if (sign & GG_SIGN_LESS) {
        return memVal < qVal1;
    }
    if (sign & GG_SIGN_LESS_OR_EQUAL) {
        return memVal <= qVal1;
    }
    if (sign & GG_SIGN_GREATER) {
        return memVal > qVal1;
    }
    if (sign & GG_SIGN_GREATER_OR_EQUAL) {
        return memVal >= qVal1;
    }

    return false;
}

bool LuaMemoryScanner::readValue(uintptr_t addr, uint32_t type, std::string& outVal) {
    uint8_t buf[8] = {0};
    size_t sz = (type == GG_TYPE_BYTE) ? 1 : (type == GG_TYPE_WORD) ? 2 : (type == GG_TYPE_QWORD || type == GG_TYPE_DOUBLE) ? 8 : 4;
    if (!safeRead(addr, buf, sz)) return false;

    switch (type) {
        case GG_TYPE_BYTE: {
            uint8_t v = buf[0];
            outVal = std::to_string(v);
            return true;
        }
        case GG_TYPE_WORD: {
            int16_t v = 0;
            std::memcpy(&v, buf, 2);
            outVal = std::to_string(v);
            return true;
        }
        case GG_TYPE_DWORD:
        case GG_TYPE_AUTO: {
            int32_t v = 0;
            std::memcpy(&v, buf, 4);
            outVal = std::to_string(v);
            return true;
        }
        case GG_TYPE_QWORD: {
            int64_t v = 0;
            std::memcpy(&v, buf, 8);
            outVal = std::to_string(v);
            return true;
        }
        case GG_TYPE_FLOAT: {
            float v = 0.0f;
            std::memcpy(&v, buf, 4);
            outVal = std::to_string(v);
            return true;
        }
        case GG_TYPE_DOUBLE: {
            double v = 0.0;
            std::memcpy(&v, buf, 8);
            outVal = std::to_string(v);
            return true;
        }
        default:
            return false;
    }
}

bool LuaMemoryScanner::matchValue(uintptr_t addr, uint32_t type, const std::string& query, uint32_t sign, std::string* outVal) {
    double qVal1 = 0, qVal2 = 0;
    bool isRange = false;
    if (!parseQuery(query, type, qVal1, qVal2, isRange)) return false;

    uint8_t buf[8] = {0};
    size_t sz = (type == GG_TYPE_BYTE) ? 1 : (type == GG_TYPE_WORD) ? 2 : (type == GG_TYPE_QWORD || type == GG_TYPE_DOUBLE) ? 8 : 4;

    if (!safeRead(addr, buf, sz)) return false;
    return matchBufferValue(buf, type, qVal1, qVal2, isRange, sign, outVal);
}

bool LuaMemoryScanner::searchNumber(const std::string& query, uint32_t type,
                                    uint32_t sign, uintptr_t memFrom,
                                    uintptr_t memTo, size_t limit) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_results.clear();

    if (type == GG_TYPE_AUTO) type = GG_TYPE_DWORD;

    double qVal1 = 0, qVal2 = 0;
    bool isRange = false;
    if (!parseQuery(query, type, qVal1, qVal2, isRange)) {
        LOGE("Failed to parse search query: %s", query.c_str());
        return false;
    }

    std::vector<MemoryRange> ranges;
    refreshRanges(ranges);

    size_t valSize = (type == GG_TYPE_BYTE) ? 1 : (type == GG_TYPE_WORD) ? 2 : (type == GG_TYPE_QWORD || type == GG_TYPE_DOUBLE) ? 8 : 4;

    constexpr size_t CHUNK_SIZE = 64 * 1024;
    std::vector<uint8_t> buffer(CHUNK_SIZE + valSize);

    for (const auto& r : ranges) {
        if ((m_targetRegions != GG_REGION_ALL) && !(r.regionType & m_targetRegions)) {
            continue;
        }

        uintptr_t segStart = (memFrom != 0) ? std::max(r.start, memFrom) : r.start;
        uintptr_t segEnd   = (memTo != 0)   ? std::min(r.end, memTo)     : r.end;
        if (segStart >= segEnd) continue;

        size_t segSize = segEnd - segStart;
        if (segSize < valSize || segSize > 256 * 1024 * 1024) continue;

        for (uintptr_t cur = segStart; cur < segEnd; cur += CHUNK_SIZE) {
            size_t bytesToRead = std::min(CHUNK_SIZE + valSize, segEnd - cur);
            if (!safeRead(cur, buffer.data(), bytesToRead)) continue;

            size_t maxOffset = (bytesToRead >= valSize) ? (bytesToRead - valSize) : 0;
            for (size_t offset = 0; offset <= maxOffset; offset += valSize) {
                uintptr_t addr = cur + offset;
                const uint8_t* ptr = buffer.data() + offset;
                std::string valStr;
                if (matchBufferValue(ptr, type, qVal1, qVal2, isRange, sign, &valStr)) {
                    SearchResult sr;
                    sr.address = addr;
                    sr.type = type;
                    sr.valueStr = valStr;
                    sr.name = r.name;
                    m_results.push_back(sr);

                    if (m_results.size() >= limit) {
                        return true;
                    }
                }
            }
        }
    }

    return !m_results.empty();
}

bool LuaMemoryScanner::refineNumber(const std::string& query, uint32_t type, uint32_t sign) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_results.empty()) return false;

    if (type == GG_TYPE_AUTO) type = GG_TYPE_DWORD;

    std::vector<SearchResult> refined;
    for (const auto& item : m_results) {
        std::string valStr;
        if (matchValue(item.address, type, query, sign, &valStr)) {
            SearchResult sr = item;
            sr.type = type;
            sr.valueStr = valStr;
            refined.push_back(sr);
        }
    }

    m_results = std::move(refined);
    return !m_results.empty();
}

void LuaMemoryScanner::clearResults() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_results.clear();
}

size_t LuaMemoryScanner::getResultsCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_results.size();
}

void LuaMemoryScanner::setResults(const std::vector<SearchResult>& results) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_results = results;
}

bool LuaMemoryScanner::searchPointer(size_t maxOffset, uintptr_t memFrom, uintptr_t memTo, size_t limit) {
    std::vector<SearchResult> targets;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        targets = m_results;
        m_results.clear();
    }
    if (targets.empty()) return false;

    std::vector<MemoryRange> ranges;
    refreshRanges(ranges);

    constexpr size_t CHUNK_SIZE = 64 * 1024;
    std::vector<uint8_t> buffer(CHUNK_SIZE + sizeof(uintptr_t));
    std::vector<SearchResult> foundPointers;

    for (const auto& r : ranges) {
        if ((m_targetRegions != GG_REGION_ALL) && !(r.regionType & m_targetRegions)) {
            continue;
        }

        uintptr_t segStart = (memFrom != 0) ? std::max(r.start, memFrom) : r.start;
        uintptr_t segEnd   = (memTo != 0)   ? std::min(r.end, memTo)     : r.end;
        if (segStart >= segEnd) continue;

        size_t segSize = segEnd - segStart;
        if (segSize < sizeof(uintptr_t) || segSize > 256 * 1024 * 1024) continue;

        for (uintptr_t cur = segStart; cur < segEnd; cur += CHUNK_SIZE) {
            size_t bytesToRead = std::min(CHUNK_SIZE + sizeof(uintptr_t), segEnd - cur);
            if (!safeRead(cur, buffer.data(), bytesToRead)) continue;

            size_t maxOff = (bytesToRead >= sizeof(uintptr_t)) ? (bytesToRead - sizeof(uintptr_t)) : 0;
            for (size_t offset = 0; offset <= maxOff; offset += 4) {
                uintptr_t val = 0;
                memcpy(&val, buffer.data() + offset, sizeof(uintptr_t));

                for (const auto& tgt : targets) {
                    if (val >= tgt.address && val <= tgt.address + maxOffset) {
                        SearchResult sr;
                        sr.address = cur + offset;
                        sr.type = (sizeof(uintptr_t) == 8) ? GG_TYPE_QWORD : GG_TYPE_DWORD;
                        sr.valueStr = std::to_string(val);
                        sr.name = r.name;
                        foundPointers.push_back(sr);

                        if (limit > 0 && foundPointers.size() >= limit) {
                            goto done;
                        }
                    }
                }
            }
        }
    }

done:
    std::lock_guard<std::mutex> lock(m_mutex);
    m_results = std::move(foundPointers);
    return !m_results.empty();
}

std::vector<SearchResult> LuaMemoryScanner::getResults(size_t count, size_t offset) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (offset >= m_results.size()) return {};

    size_t endIdx = std::min(m_results.size(), offset + count);
    std::vector<SearchResult> out;
    out.reserve(endIdx - offset);

    for (size_t i = offset; i < endIdx; ++i) {
        SearchResult sr = m_results[i];
        readValue(sr.address, sr.type, sr.valueStr);
        out.push_back(sr);
    }
    return out;
}

bool LuaMemoryScanner::getValues(std::vector<SearchResult>& items) {
    for (auto& item : items) {
        readValue(item.address, item.type, item.valueStr);
    }
    return true;
}

bool LuaMemoryScanner::setValues(const std::vector<SearchResult>& items) {
    for (const auto& item : items) {
        switch (item.type) {
            case GG_TYPE_BYTE: {
                uint8_t v = static_cast<uint8_t>(std::stoul(item.valueStr));
                safeWrite(item.address, &v, sizeof(v));
                break;
            }
            case GG_TYPE_WORD: {
                int16_t v = static_cast<int16_t>(std::stol(item.valueStr));
                safeWrite(item.address, &v, sizeof(v));
                break;
            }
            case GG_TYPE_DWORD:
            case GG_TYPE_AUTO: {
                int32_t v = static_cast<int32_t>(std::stol(item.valueStr));
                safeWrite(item.address, &v, sizeof(v));
                break;
            }
            case GG_TYPE_QWORD: {
                int64_t v = static_cast<int64_t>(std::stoll(item.valueStr));
                safeWrite(item.address, &v, sizeof(v));
                break;
            }
            case GG_TYPE_FLOAT: {
                float v = std::stof(item.valueStr);
                safeWrite(item.address, &v, sizeof(v));
                break;
            }
            case GG_TYPE_DOUBLE: {
                double v = std::stod(item.valueStr);
                safeWrite(item.address, &v, sizeof(v));
                break;
            }
            default:
                break;
        }
    }
    return true;
}

size_t LuaMemoryScanner::editAll(const std::string& valueStr, uint32_t type) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_results.empty()) return 0;

    for (auto& item : m_results) {
        item.valueStr = valueStr;
        if (type != GG_TYPE_AUTO) item.type = type;
    }
    setValues(m_results);
    return m_results.size();
}

bool LuaMemoryScanner::safeRead(uintptr_t address, void* buffer, size_t size) {
    if (!address || !buffer || !size) return false;

    // Verify memory pages are mapped and resident in RAM via kernel page tables
    if (!isMemoryResident(address, size)) {
        return false;
    }

    std::memcpy(buffer, reinterpret_cast<const void*>(address), size);
    return true;
}

bool LuaMemoryScanner::safeWrite(uintptr_t address, const void* buffer, size_t size) {
    if (!address || !buffer || !size) return false;

    if (!isMemoryResident(address, size)) {
        return false;
    }

    uintptr_t pageSize = 4096;
    uintptr_t pageStart = address & ~(pageSize - 1);
    uintptr_t pageEnd = (address + size + pageSize - 1) & ~(pageSize - 1);

    mprotect(reinterpret_cast<void*>(pageStart), pageEnd - pageStart, PROT_READ | PROT_WRITE | PROT_EXEC);
    std::memcpy(reinterpret_cast<void*>(address), buffer, size);
    return true;
}

uintptr_t LuaMemoryScanner::allocatePage(size_t size, int protection) {
    void* addr = mmap(nullptr, size, protection, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (addr == MAP_FAILED) return 0;
    return reinterpret_cast<uintptr_t>(addr);
}

} // namespace Canvas::Lua
