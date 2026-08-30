package git.artdeell.skymodloader.elfmod;

import android.app.Activity;
import android.graphics.BitmapFactory;
import android.util.Log;

import net.fornwall.jelf.ElfFile;
import net.fornwall.jelf.ElfSectionHeader;
import net.fornwall.jelf.ElfStringTable;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;

import git.artdeell.skymodloader.luamod.LuaModMetadata;
import git.artdeell.skymodloader.updater.ModUpdater;

public class ElfUIBackbone {
    private final List<ElfModUIMetadata> mods = new ArrayList<>();
    private LoadingListener listener = LoadingListener.DUMMY;
    public Activity activity;
    public ModUpdater modUpdater;
    private File modFolder;
    private volatile Exception currentException;
    private final AtomicBoolean progressBarActive = new AtomicBoolean(false);
    private volatile UnsafeRemovalMetadata unsafeRemovalMetadata;

    public int getModsCount() {
        return mods.size();
    }

    public ElfModUIMetadata getMod(int where) {
        return mods.get(where);
    }

    ElfUIBackbone(Activity activity, ModUpdater modUpdater) {
        this.activity = activity;
        this.modUpdater = modUpdater;
    }

    private void startLoading() {
        progressBarActive.set(true);
        listener.onLoadingUpdated();
    }

    private void stopLoading() {
        progressBarActive.set(false);
        listener.onLoadingUpdated();
    }

    private void notifyException(Exception e) {
        currentException = e;
        listener.signalModAddException();
    }

    public void loadMetaFromModFolder(File modFolder) {
        mods.clear();
        this.modFolder = modFolder;
        if (!modFolder.exists()) {
            modFolder.mkdirs();
        }
        File[] files = modFolder.listFiles(new SharedObjectFileFilter());
        if (files != null) {
            for (File f : files) {
                try {
                    mods.add(getElfMetadata(f));
                } catch (IOException e) {
                    e.printStackTrace();
                    ElfModUIMetadata metadata = new ElfModUIMetadata();
                    metadata.activity = this.activity;
                    metadata.modFile = f;
                    metadata.name = f.getName();
                    metadata.modIsValid = false;
                    mods.add(metadata);
                }
            }
            meta_loop:
            for (ElfModUIMetadata metadata : mods) {
                if (!metadata.modIsValid) continue;
                for (ElfModMetadata dep : metadata.dependencies) {
                    if (findCompatibleDep(dep) == null) {
                        metadata.modIsValid = false;
                        continue meta_loop;
                    }
                }
            }
        }
    }

    private ElfModUIMetadata getElfMetadata(File f) throws IOException {
        if (f.getName().endsWith(".lua")) {
            ElfModUIMetadata meta = LuaModMetadata.parseFromFile(f);
            meta.activity = this.activity;
            return meta;
        }
        FileInputStream fis = new FileInputStream(f);
        ElfModUIMetadata defaultMeta = new ElfModUIMetadata();
        defaultMeta.activity = this.activity;
        defaultMeta.name = f.getName();
        defaultMeta.modFile = f;
        getElfMetadata(defaultMeta, getBytesFromInputStream(fis));
        fis.close();
        return defaultMeta;
    }

    private ElfModUIMetadata getElfMetadata(byte[] bytes) {
        ElfModUIMetadata defaultMeta = new ElfModUIMetadata();
        defaultMeta.activity = this.activity;
        return getElfMetadata(defaultMeta, bytes);
    }

    private ElfModUIMetadata getElfMetadata(ElfModUIMetadata defaultMeta, byte[] elfFile) {
        if (LuaModMetadata.isLuaScript(elfFile) || (defaultMeta.name != null && defaultMeta.name.endsWith(".lua"))) {
            ElfModUIMetadata luaMeta = LuaModMetadata.parseFromBytes(elfFile, defaultMeta.name);
            luaMeta.activity = defaultMeta.activity;
            luaMeta.modFile = defaultMeta.modFile;
            return luaMeta;
        }
        try {
            ElfFile elf = ElfFile.from(elfFile);
            ElfStringTable shstrtab = elf.getSectionNameStringTable();
            long secoff_icon = -1;
            long secsz_icon = -1;
            long secoff_config = -1;
            long secsz_config = -1;
            for (int i = 0; i < elf.e_shnum; i++) {
                ElfSectionHeader shdr = elf.getSection(i).header;
                String shname = shstrtab.get(shdr.sh_name);
                if (".icon".equals(shname)) {
                    secoff_icon = shdr.sh_offset;
                    secsz_icon = shdr.sh_size;
                    continue;
                }
                if (".config".equals(shname)) {
                    secoff_config = shdr.sh_offset;
                    secsz_config = shdr.sh_size;
                }
            }
            if (secoff_config != -1 && secsz_config > 0 && secsz_config < Integer.MAX_VALUE) {
                byte[] config = new byte[(int) secsz_config];
                System.arraycopy(elfFile, (int) secoff_config, config, 0, config.length);
                String jsonStr = new String(config, 0, config.length).replace("\0", "").trim();
                try {
                    JSONObject jsonConfig = new JSONObject(jsonStr);
                    defaultMeta.name = jsonConfig.optString("name", defaultMeta.name != null ? defaultMeta.name : "mod");
                    defaultMeta.author = jsonConfig.optString("author", "Native Mod");
                    defaultMeta.description = jsonConfig.optString("description", "Canvas ELF Mod");
                    defaultMeta.majorVersion = jsonConfig.optInt("majorVersion", 1);
                    defaultMeta.minorVersion = jsonConfig.optInt("minorVersion", 0);
                    defaultMeta.patchVersion = jsonConfig.optInt("patchVersion", 0);
                    defaultMeta.displayName = jsonConfig.optString("displayName", defaultMeta.name);
                    defaultMeta.githubReleasesUrl = jsonConfig.optString("githubReleasesUrl");
                    defaultMeta.offsetsUrl = jsonConfig.optString("offsetsUrl");
                    defaultMeta.displaysUI = jsonConfig.optBoolean("displaysUI", true);
                    defaultMeta.selfManagedUI = jsonConfig.optBoolean("selfManagedUI", false);
                    JSONArray jdeps = jsonConfig.optJSONArray("dependencies");
                    if (jdeps != null) {
                        ElfModMetadata[] dependencies = new ElfModMetadata[jdeps.length()];
                        for (int i = 0; i < dependencies.length; i++) {
                            JSONObject jsonDependency = jdeps.getJSONObject(i);
                            ElfModMetadata dependency = new ElfModMetadata();
                            dependency.modIsValid = true;
                            dependency.name = jsonDependency.optString("name", "dep");
                            dependency.author = jsonDependency.optString("author");
                            dependency.description = jsonDependency.optString("description");
                            dependency.majorVersion = jsonDependency.optInt("majorVersion", 1);
                            dependency.minorVersion = jsonDependency.optInt("minorVersion", 0);
                            dependency.patchVersion = jsonDependency.optInt("patchVersion", 0);
                            dependencies[i] = dependency;
                        }
                        defaultMeta.dependencies = dependencies;
                    } else {
                        defaultMeta.dependencies = new ElfModMetadata[0];
                    }
                    defaultMeta.modIsValid = true;
                } catch (Exception e) {
                    e.printStackTrace();
                }
            }
            if (!defaultMeta.modIsValid) {
                // Fallback for raw / stripped ELF .so mods
                String fallbackName = defaultMeta.name != null ? defaultMeta.name : "mod_" + System.currentTimeMillis() + ".so";
                defaultMeta.name = fallbackName;
                defaultMeta.displayName = fallbackName.replace(".so", "").replace("lib", "");
                defaultMeta.author = "Native Mod";
                defaultMeta.description = "Canvas Native ELF Mod";
                defaultMeta.majorVersion = 1;
                defaultMeta.minorVersion = 0;
                defaultMeta.patchVersion = 0;
                defaultMeta.displaysUI = true;
                defaultMeta.dependencies = new ElfModMetadata[0];
                defaultMeta.modIsValid = true;
            }
            if (secsz_icon > 0 && secsz_icon < Integer.MAX_VALUE) {
                try {
                    byte[] icon = new byte[(int) secsz_icon];
                    System.arraycopy(elfFile, (int) secoff_icon, icon, 0, icon.length);
                    defaultMeta.bitmapIcon = BitmapFactory.decodeByteArray(icon, 0, icon.length);
                } catch (Exception e) {
                    defaultMeta.bitmapIcon = null;
                    e.printStackTrace();
                }
            }
            return defaultMeta;
        } catch (Exception e) {
            e.printStackTrace();
            defaultMeta.modIsValid = false;
            return defaultMeta;
        }
    }

    private ElfModMetadata findCompatibleDep(ElfModMetadata depInfo) {
        ElfModUIMetadata metadata = null;
        for (ElfModUIMetadata metadata1 : mods) {
            if (depInfo.name.equals(metadata1.name) && metadata1.modIsValid) {
                metadata = metadata1;
                break;
            }
        }
        Log.i("ElfLdr", "depInfo.name=" + depInfo.name);
        if (metadata != null) {
            Log.i("ElfLdr", "depInfo.minor=" + depInfo.minorVersion + ";metadata.minor=" + metadata.minorVersion);
            if (depInfo.majorVersion != metadata.majorVersion) return null;
            if (depInfo.minorVersion > metadata.minorVersion) return null;
        }
        return metadata;
    }

    private void loadFileFromInputStream(InputStream inputStream, String suggestedName) throws IOException, NoDependenciesException, InvalidModException, ModExistsException {
        byte[] bytes = getBytesFromInputStream(inputStream);
        inputStream.close();

        ElfModUIMetadata metadata;
        boolean isLua = (suggestedName != null && suggestedName.endsWith(".lua")) || LuaModMetadata.isLuaScript(bytes);
        if (isLua) {
            String name = suggestedName != null ? suggestedName : "script_" + System.currentTimeMillis() + ".lua";
            metadata = LuaModMetadata.parseFromBytes(bytes, name);
            metadata.activity = this.activity;
        } else {
            metadata = getElfMetadata(bytes);
        }

        if (!metadata.modIsValid) throw new InvalidModException();
        ArrayList<ElfModMetadata> badDependencies = new ArrayList<>();
        for (ElfModMetadata dep : metadata.dependencies) {
            if (findCompatibleDep(dep) == null) {
                metadata.modIsValid = false;
                badDependencies.add(dep);
            }
        }
        if (!badDependencies.isEmpty()) {
            throw new NoDependenciesException(metadata, badDependencies.toArray(new ElfModMetadata[0]));
        } else {
            if (findSameMod(metadata.name)) throw new ModExistsException();
            File modFile = new File(modFolder, metadata.name);
            FileOutputStream fos = new FileOutputStream(modFile);
            fos.write(bytes);
            fos.close();
            metadata.modFile = modFile;
            mods.add(metadata);
        }
    }

    public void addModSafely(InputStream stream, String suggestedName) {
        new Thread(() -> {
            startLoading();
            try {
                loadFileFromInputStream(stream, suggestedName);
                listener.refreshModList(1, getModsCount() - 1);
            } catch (Exception e) {
                notifyException(e);
            }
            stopLoading();
        }).start();
    }

    public void addModSafely(InputStream stream) {
        addModSafely(stream, null);
    }

    public void removeModSafelyAsync(int which) {
        new Thread(() -> {
            startLoading();
            ElfModUIMetadata reqModMeta = getMod(which);
            ArrayList<ElfModUIMetadata> dependingMods = new ArrayList<>();
            if (reqModMeta.modIsValid) {
                for (ElfModUIMetadata meta : mods) {
                    if (!meta.modIsValid) continue;
                    for (ElfModMetadata dep : meta.dependencies) {
                        if (dep.name.equals(reqModMeta.name)) {
                            dependingMods.add(meta);
                            break;
                        }
                    }
                }
            }
            if (dependingMods.isEmpty()) {
                if (reqModMeta.modFile.delete()) {
                    mods.remove(reqModMeta);
                    listener.refreshModList(0, which);
                } else {
                    listener.signalModRemovalError();
                }
            } else {
                unsafeRemovalMetadata = new UnsafeRemovalMetadata(reqModMeta, dependingMods);
                listener.signalModRemovalUnsafe();
            }
            stopLoading();
        }).start();
    }

    private static byte[] getBytesFromInputStream(InputStream is) throws IOException {
        ByteArrayOutputStream os = new ByteArrayOutputStream();
        byte[] buffer = new byte[0xFFFF];
        for (int len = is.read(buffer); len != -1; len = is.read(buffer)) {
            os.write(buffer, 0, len);
        }
        return os.toByteArray();
    }

    public void addListener(LoadingListener listener) {
        this.listener = listener;
    }

    public void removeListener() {
        this.listener = LoadingListener.DUMMY;
    }

    public Exception getException() {
        return currentException;
    }

    public void resetException() {
        currentException = null;
    }

    public boolean getProgressBarState() {
        return progressBarActive.get();
    }

    public UnsafeRemovalMetadata getUnsafeRemovalMetadata() {
        return unsafeRemovalMetadata;
    }

    public void resetModRemovalMetadata() {
        unsafeRemovalMetadata = null;
    }

    public void startLoadingAsync(final File modsFolder) {
        new Thread(() -> {
            startLoading();
            loadMetaFromModFolder(modsFolder);
            stopLoading();
            listener.refreshModList(3, 0);
        }).start();
    }

    public static class UnsafeRemovalMetadata {
        public final ElfModUIMetadata removingMod;
        public final List<ElfModUIMetadata> dependingMods;

        public UnsafeRemovalMetadata(ElfModUIMetadata removingMod, List<ElfModUIMetadata> dependingMods) {
            this.removingMod = removingMod;
            this.dependingMods = dependingMods;
        }
    }

    private boolean findSameMod(String name) {
        for (ElfModUIMetadata mod : mods) {
            if (mod.name.equals(name)) return true;
        }
        return false;
    }

    public int getModIndex(ElfModMetadata elfMod) {
        for (int i = 0; i < mods.size(); i++) {
            if (mods.get(i).name.equals(elfMod.name)) return i;
        }
        return -1;
    }

    public Boolean isModMetadataValid(File file) {
        try {
            ElfModUIMetadata elfMod = getElfMetadata(file);
            return elfMod.modIsValid;
        } catch (IOException io) {

        }

        return false;
    }
}
