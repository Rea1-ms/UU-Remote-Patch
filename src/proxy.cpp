#include "common.hpp"
#include <fstream>

extern "C" { FARPROC uu_version_exports[17]{}; }
DWORD WINAPI patch_worker(void* module);
static BOOL initialize_proxy(HINSTANCE instance) {
    DisableThreadLibraryCalls(instance);
    // The system directory fits MAX_PATH. Never put a 64 KiB buffer in DllMain:
    // static TLS may prevent disabling notifications on small host threads.
    wchar_t system[MAX_PATH]{};
    if(!GetSystemDirectoryW(system,DWORD(std::size(system))))return FALSE;
    auto path=uu::fs::path(system)/L"version.dll";
    HMODULE original=LoadLibraryW(path.c_str());
    if(!original)return FALSE;
    static constexpr const char* names[]={"GetFileVersionInfoA","GetFileVersionInfoByHandle","GetFileVersionInfoExA",
        "GetFileVersionInfoExW","GetFileVersionInfoSizeA","GetFileVersionInfoSizeExA","GetFileVersionInfoSizeExW",
        "GetFileVersionInfoSizeW","GetFileVersionInfoW","VerFindFileA","VerFindFileW","VerInstallFileA","VerInstallFileW",
        "VerLanguageNameA","VerLanguageNameW","VerQueryValueA","VerQueryValueW"};
    for(size_t i=0;i<std::size(names);++i) {
        uu_version_exports[i]=GetProcAddress(original,names[i]);
        if(!uu_version_exports[i])return FALSE;
    }
    HANDLE thread=CreateThread(nullptr,1024*1024,patch_worker,instance,STACK_SIZE_PARAM_IS_A_RESERVATION,nullptr);
    if(thread)CloseHandle(thread);
    return TRUE;
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason!=DLL_PROCESS_ATTACH)return TRUE;
    return initialize_proxy(instance);
}
