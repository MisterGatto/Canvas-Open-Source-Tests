#include "Canvas.h"

#include "../../include/misc/Utils.h"
#include "../../include/misc/visibility.h"


namespace Canvas {

    PUBLIC_API DeviceInfo deviceInfo;
    PUBLIC_API const char* gameHost;
    PUBLIC_API const char* libName;
    PUBLIC_API std::uintptr_t libBase;
    PUBLIC_API KittyScanner::ElfScanner libElfScanner;

    PUBLIC_API std::vector<UserLib> userLibs;
    PUBLIC_API void pushUserLib(UserLib& _userLib) {
        Canvas::userLibs.push_back(_userLib);
    }

    PUBLIC_API bool isLibLoaded(const char* _elfName) {
        return isLibraryLoaded(_elfName);
    }

    PUBLIC_API std::uintptr_t findLib(const char* _elfName) {
        return findLibrary(_elfName);
    }

    PUBLIC_API void CanvasMenu();

    PUBLIC_API AAssetManager* aAssetManager;
    PUBLIC_API bool dev = false;
    PUBLIC_API _Atomic std::uint32_t gameVersion;
    PUBLIC_API int gameType;
    PUBLIC_API bool frameRateLimited;
    PUBLIC_API JavaVM *javaVM;
    PUBLIC_API JNIEnv *jniEnv;
    PUBLIC_API jobject systemUI;
    PUBLIC_API jclass MainActivity;
    PUBLIC_API bool CeserverEnabled;
    PUBLIC_API bool hideCanvasMenu;
    PUBLIC_API const char *configsPath;

    PUBLIC_API std::vector<void (*)(std::string)> onKeyboardCompleteListeners;
}