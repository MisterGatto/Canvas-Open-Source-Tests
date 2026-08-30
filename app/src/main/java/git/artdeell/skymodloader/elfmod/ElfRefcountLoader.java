package git.artdeell.skymodloader.elfmod;

import android.util.ArrayMap;

import net.fornwall.jelf.ElfFile;
import net.fornwall.jelf.ElfSectionHeader;
import net.fornwall.jelf.ElfStringTable;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Optional;

import git.artdeell.skymodloader.ElfLoader;
import git.artdeell.skymodloader.LibrarySelectorListener;

public class ElfRefcountLoader extends ElfLoader{
    private final List<ElfFileReference> elfFileReferences = new ArrayList<>();
    private final Map<String, ElfModMetadata> metadataByName = new ArrayMap<>();
    private final File modsFolder;
    public ElfRefcountLoader(String defaultPaths, File modsFolder) throws IOException {
        super(defaultPaths+":"+modsFolder.getAbsolutePath());
        this.modsFolder = modsFolder;
        if(!modsFolder.exists()) {
            modsFolder.mkdirs();
        }
    }

    public void load() throws Exception {
        File[] modFiles = modsFolder.listFiles(new SharedObjectFileFilter());
        if(modFiles == null) return;
        for(File f : modFiles) {
            try {
                if (f.getName().endsWith(".so")&&(!new File(f.getPath() + "_invalid.txt").exists())) {
                    addElf(f);
                }
            } catch (InvalidModException  e) {
                throw new InvalidModException("Failed to load mod" + f.getName());
            }
        }
        scanDeps();
        Collections.sort(elfFileReferences);
        for(ElfFileReference ref : elfFileReferences) {
            loadLib(ref.modMeta.name);
        }
    }

    @Override
    public void loadNative(String absolutePath, String name) {
        if (absolutePath.startsWith(modsFolder.getAbsolutePath())) {
            ElfModMetadata metadata = metadataByName.get(name);
            if (metadata == null) {
                throw new IllegalStateException("WTF? No saved metadata for mod library " + name);
            }

            LibrarySelectorListener.onModLibrary(
                    absolutePath,
                    metadata.displaysUI,
                    Optional.ofNullable(metadata.displayName).orElse(metadata.name),
                    Optional.ofNullable(metadata.author).orElse(""),
                    Optional.ofNullable(metadata.description).orElse(""),
                    metadata.majorVersion + "." + metadata.minorVersion + "." + metadata.patchVersion,
                    metadata.selfManagedUI
            );
        } else {
            super.loadNative(absolutePath, name);
        }
    }

    public void addElf(File file) throws Exception {
        ElfModMetadata metadata = loadMetadata(file);
        elfFileReferences.add(new ElfFileReference(metadata));
        metadataByName.put(metadata.name, metadata);
    }

    public static ElfModMetadata loadMetadata(File file) throws Exception {
        ElfFile elf = ElfFile.from(file);
        RandomAccessFile raf = new RandomAccessFile(file,"r");
        ElfStringTable shstrtab = elf.getSectionNameStringTable();
        long secoff_config=-1; long secsz_config = -1;
        for(int i = 0; i < elf.e_shnum; i++) {
            ElfSectionHeader shdr = elf.getSection(i).header;
            String shname = shstrtab.get(shdr.sh_name);
            if(".config".equals(shname)) {
                secoff_config = shdr.sh_offset;
                secsz_config = shdr.sh_size;
            }
        }
        if(secoff_config == -1 || secsz_config <= 0 || secsz_config > Integer.MAX_VALUE) {
            raf.close();
            ElfModMetadata fallback = new ElfModMetadata();
            fallback.name = file.getName();
            fallback.displayName = file.getName().replace(".so", "").replace("lib", "");
            fallback.author = "Native Mod";
            fallback.description = "Canvas Native ELF Mod";
            fallback.majorVersion = 1;
            fallback.minorVersion = 0;
            fallback.patchVersion = 0;
            fallback.displaysUI = true;
            fallback.dependencies = new ElfModMetadata[0];
            fallback.modIsValid = true;
            return fallback;
        }
        byte[] config = new byte[(int) secsz_config];
        raf.seek(secoff_config);
        raf.readFully(config);
        raf.close();

        String jsonString = new String(config).replace("\0", "").trim();
        try {
            JSONObject jsonConfig = new JSONObject(jsonString);
            ElfModMetadata modMetadata = new ElfModMetadata();
            modMetadata.name = jsonConfig.optString("name", file.getName());
            modMetadata.author = jsonConfig.optString("author", "Native Mod");
            modMetadata.description = jsonConfig.optString("description", "Canvas ELF Mod");
            modMetadata.majorVersion = jsonConfig.optInt("majorVersion", 1);
            modMetadata.minorVersion = jsonConfig.optInt("minorVersion", 0);
            modMetadata.patchVersion = jsonConfig.optInt("patchVersion", 0);
            modMetadata.displayName = jsonConfig.optString("displayName", modMetadata.name);
            modMetadata.displaysUI = jsonConfig.optBoolean("displaysUI", true);
            modMetadata.selfManagedUI = jsonConfig.optBoolean("selfManagedUI", false);
            JSONArray jdeps = jsonConfig.optJSONArray("dependencies");
            if (jdeps != null) {
                ElfModMetadata[] dependencies = new ElfModMetadata[jdeps.length()];
                for (int i = 0; i < dependencies.length; i++) {
                    JSONObject jsonDependency = jdeps.getJSONObject(i);
                    ElfModMetadata dependency = new ElfModMetadata();
                    dependency.modIsValid = true;
                    dependency.name = jsonDependency.optString("name", "dep");
                    dependency.author = jsonDependency.optString("author");
                    dependency.majorVersion = jsonDependency.optInt("majorVersion", 1);
                    dependency.minorVersion = jsonDependency.optInt("minorVersion", 0);
                    dependency.patchVersion = jsonDependency.optInt("patchVersion", 0);
                    dependencies[i] = dependency;
                }
                modMetadata.dependencies = dependencies;
            } else {
                modMetadata.dependencies = new ElfModMetadata[0];
            }
            modMetadata.modIsValid = true;
            return modMetadata;
        } catch (Exception e) {
            e.printStackTrace();
            ElfModMetadata fallback = new ElfModMetadata();
            fallback.name = file.getName();
            fallback.displayName = file.getName().replace(".so", "").replace("lib", "");
            fallback.author = "Native Mod";
            fallback.description = "Canvas Native ELF Mod";
            fallback.majorVersion = 1;
            fallback.minorVersion = 0;
            fallback.patchVersion = 0;
            fallback.displaysUI = true;
            fallback.dependencies = new ElfModMetadata[0];
            fallback.modIsValid = true;
            return fallback;
        }
    }

    public void scanDeps() {
        ElfFileReference dummyReference = new ElfFileReference(null);
        for(ElfFileReference reference : elfFileReferences) {
            for(ElfModMetadata deps : reference.modMeta.dependencies) {
                dummyReference.modMeta = deps;
                int index = elfFileReferences.indexOf(dummyReference);
                if(index == -1) throw new IllegalStateException("Can't find dependency "+deps.name);
                else {
                    ElfFileReference target = elfFileReferences.get(index);
                    ElfModMetadata metadata = target.modMeta;
                    if(metadata.majorVersion != deps.majorVersion) throw new IllegalStateException(metadata.name+ ": Amajor "+metadata.majorVersion+" != Dmajor "+deps.majorVersion);
                    if(metadata.minorVersion < deps.minorVersion) throw new IllegalStateException(metadata.name+": Aminor "+metadata.minorVersion+" < Dminor "+deps.minorVersion);
                    target.referenceCount++;
                }
            }
        }
    }
}
