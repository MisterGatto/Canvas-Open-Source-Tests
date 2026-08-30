#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include <memory>

namespace Canvas::Lua {

// GameGuardian data type constants
enum GGDataType : uint32_t {
    GG_TYPE_AUTO   = 127,
    GG_TYPE_BYTE   = 1,
    GG_TYPE_WORD   = 2,
    GG_TYPE_DWORD  = 4,
    GG_TYPE_XOR    = 8,
    GG_TYPE_FLOAT  = 16,
    GG_TYPE_QWORD  = 32,
    GG_TYPE_DOUBLE = 64
};

// GameGuardian region constants
enum GGRegion : uint32_t {
    GG_REGION_ALL          = 0xFFFFFFFF,
    GG_REGION_ANONYMOUS    = 1 << 0,
    GG_REGION_CODE_APP     = 1 << 1,
    GG_REGION_CODE_SYS     = 1 << 2,
    GG_REGION_C_ALLOC      = 1 << 3,
    GG_REGION_C_DATA       = 1 << 4,
    GG_REGION_C_BSS        = 1 << 5,
    GG_REGION_C_HEAP       = 1 << 6,
    GG_REGION_JAVA_HEAP    = 1 << 7,
    GG_REGION_JAVA         = 1 << 8,
    GG_REGION_STACK        = 1 << 9,
    GG_REGION_ASHMEM       = 1 << 10,
    GG_REGION_BAD          = 1 << 11,
    GG_REGION_OTHER        = 1 << 12
};

// Search comparison operators
enum GGSign : uint32_t {
    GG_SIGN_EQUAL             = 0x1,
    GG_SIGN_NOT_EQUAL         = 0x2,
    GG_SIGN_LESS_OR_EQUAL     = 0x4,
    GG_SIGN_GREATER_OR_EQUAL  = 0x8,
    GG_SIGN_LESS              = 0x10,
    GG_SIGN_GREATER           = 0x20
};

struct MemoryRange {
    uintptr_t start = 0;
    uintptr_t end = 0;
    uint32_t regionType = GG_REGION_OTHER;
    int protection = 0; // PROT_READ, PROT_WRITE, PROT_EXEC
    std::string perms;  // e.g. "r-xp", "rw-p"
    std::string name;
    std::string path;
};

struct SearchResult {
    uintptr_t address = 0;
    uint32_t type = GG_TYPE_DWORD;
    std::string valueStr;
    std::string name;
    bool freeze = false;
};

class LuaMemoryScanner {
public:
    LuaMemoryScanner();
    ~LuaMemoryScanner() = default;

    // Region configuration
    void setTargetRegions(uint32_t regionsMask);
    uint32_t getTargetRegions() const;
    std::vector<MemoryRange> getMemoryRanges(const std::string& filter = "");

    // Search operations
    bool searchNumber(const std::string& query, uint32_t type = GG_TYPE_AUTO,
                      uint32_t sign = GG_SIGN_EQUAL, uintptr_t memFrom = 0,
                      uintptr_t memTo = 0, size_t limit = 10000);

    bool refineNumber(const std::string& query, uint32_t type = GG_TYPE_AUTO,
                      uint32_t sign = GG_SIGN_EQUAL);

    void clearResults();
    size_t getResultsCount() const;
    std::vector<SearchResult> getResults(size_t count = 100, size_t offset = 0);
    void setResults(const std::vector<SearchResult>& results);
    bool searchPointer(size_t maxOffset = 0, uintptr_t memFrom = 0, uintptr_t memTo = 0, size_t limit = 10000);

    // Value reading / editing
    bool getValues(std::vector<SearchResult>& items);
    bool setValues(const std::vector<SearchResult>& items);
    size_t editAll(const std::string& valueStr, uint32_t type);

    // Raw memory utilities
    static bool safeRead(uintptr_t address, void* buffer, size_t size);
    static bool safeWrite(uintptr_t address, const void* buffer, size_t size);
    static bool readValue(uintptr_t addr, uint32_t type, std::string& outVal);

    static uintptr_t allocatePage(size_t size, int protection = 0x3 /* PROT_READ|PROT_WRITE */);

private:
    uint32_t m_targetRegions = GG_REGION_ANONYMOUS | GG_REGION_C_ALLOC | GG_REGION_C_DATA | GG_REGION_C_BSS;
    std::vector<SearchResult> m_results;
    mutable std::mutex m_mutex;

    static void refreshRanges(std::vector<MemoryRange>& ranges);
    static uint32_t classifyRegion(const std::string& path, int perms);
    static bool matchValue(uintptr_t addr, uint32_t type, const std::string& query, uint32_t sign, std::string* outVal = nullptr);
    static bool parseQuery(const std::string& query, uint32_t type, double& outVal1, double& outVal2, bool& isRange);
};

} // namespace Canvas::Lua
