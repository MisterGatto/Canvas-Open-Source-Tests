package git.artdeell.skymodloader.luamod;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.StringReader;
import git.artdeell.skymodloader.elfmod.ElfModMetadata;
import git.artdeell.skymodloader.elfmod.ElfModUIMetadata;

public class LuaModMetadata {

    public static boolean isLuaScript(byte[] data) {
        if (data == null || data.length < 2) return false;
        // Check for ELF magic "\x7fELF"
        if (data.length >= 4 && data[0] == 0x7F && data[1] == 'E' && data[2] == 'L' && data[3] == 'F') {
            return false;
        }
        // Non-ELF files (text or Lua bytecode) are treated as Lua scripts
        return true;
    }

    public static ElfModUIMetadata parseFromBytes(byte[] data, String fallbackName) {
        ElfModUIMetadata meta = new ElfModUIMetadata();
        meta.name = fallbackName != null ? fallbackName : "script.lua";
        meta.displayName = fallbackName != null ? fallbackName.replace(".lua", "") : "Lua Script";
        meta.author = "Lua Script";
        meta.description = "GameGuardian / Canvas Lua Mod";
        meta.majorVersion = 1;
        meta.minorVersion = 0;
        meta.patchVersion = 0;
        meta.displaysUI = true;
        meta.selfManagedUI = false;
        meta.dependencies = new ElfModMetadata[0];
        meta.modIsValid = true;

        if (data == null) return meta;

        try (BufferedReader reader = new BufferedReader(new StringReader(new String(data)))) {
            parseLines(reader, meta);
        } catch (Exception ignored) {}

        return meta;
    }

    public static ElfModUIMetadata parseFromFile(File file) {
        ElfModUIMetadata meta = new ElfModUIMetadata();
        meta.modFile = file;
        meta.name = file.getName();
        meta.displayName = file.getName().replace(".lua", "");
        meta.author = "Lua Script";
        meta.description = "GameGuardian / Canvas Lua Mod";
        meta.majorVersion = 1;
        meta.minorVersion = 0;
        meta.patchVersion = 0;
        meta.displaysUI = true;
        meta.selfManagedUI = false;
        meta.dependencies = new ElfModMetadata[0];
        meta.modIsValid = true;

        try (BufferedReader reader = new BufferedReader(new FileReader(file))) {
            parseLines(reader, meta);
        } catch (Exception ignored) {}

        return meta;
    }

    private static void parseLines(BufferedReader reader, ElfModUIMetadata meta) throws Exception {
        String line;
        int linesChecked = 0;
        while ((line = reader.readLine()) != null && linesChecked < 50) {
            linesChecked++;
            line = line.trim();
            if (!line.startsWith("--")) continue;

            String comment = line.substring(2).trim();
            if (comment.startsWith("@")) comment = comment.substring(1);

            int colonIdx = comment.indexOf(':');
            if (colonIdx == -1) colonIdx = comment.indexOf('=');

            if (colonIdx != -1) {
                String key = comment.substring(0, colonIdx).trim().toLowerCase();
                String value = comment.substring(colonIdx + 1).trim();

                switch (key) {
                    case "name":
                    case "displayname":
                    case "title":
                        meta.displayName = value;
                        break;
                    case "author":
                        meta.author = value;
                        break;
                    case "version":
                        parseVersion(value, meta);
                        break;
                    case "description":
                    case "desc":
                        meta.description = value;
                        break;
                }
            }
        }
    }

    private static void parseVersion(String ver, ElfModUIMetadata meta) {
        try {
            String[] parts = ver.split("\\.");
            if (parts.length >= 1) meta.majorVersion = Integer.parseInt(parts[0]);
            if (parts.length >= 2) meta.minorVersion = Integer.parseInt(parts[1]);
            if (parts.length >= 3) meta.patchVersion = Integer.parseInt(parts[2]);
        } catch (Exception ignored) {}
    }
}
