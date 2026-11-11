#include <windows.h>
#include <filesystem>
#include <iostream>
#include <vector>
#include <stdexcept>
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static DWORD WINAPI small_thread(void*) { return 0; }
int wmain(int argc,wchar_t** argv) {
    try {
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        check(argc==2,"supply the candidate proxy path");
        auto proxy=LoadLibraryW(std::filesystem::canonical(argv[1]).c_str());
        check(proxy!=nullptr,"proxy loading failed");
        wchar_t system[MAX_PATH]{};check(GetSystemDirectoryW(system,MAX_PATH)!=0,"system directory");
        auto original=LoadLibraryW((std::filesystem::path(system)/L"version.dll").c_str());
        check(original && original!=proxy,"proxy forwarded to itself");
        for(auto name:{"GetFileVersionInfoA","GetFileVersionInfoByHandle","GetFileVersionInfoExA",
            "GetFileVersionInfoExW","GetFileVersionInfoSizeA","GetFileVersionInfoSizeExA","GetFileVersionInfoSizeExW",
            "GetFileVersionInfoSizeW","GetFileVersionInfoW","VerFindFileA","VerFindFileW","VerInstallFileA","VerInstallFileW",
            "VerLanguageNameA","VerLanguageNameW","VerQueryValueA","VerQueryValueW"})
            check(GetProcAddress(proxy,name)!=nullptr,"missing proxy export");
        using Size=DWORD(WINAPI*)(LPCWSTR,LPDWORD);
        using Info=BOOL(WINAPI*)(LPCWSTR,DWORD,DWORD,LPVOID);
        using Query=BOOL(WINAPI*)(LPCVOID,LPCWSTR,LPVOID*,PUINT);
        auto size=[&](HMODULE m){return reinterpret_cast<Size>(GetProcAddress(m,"GetFileVersionInfoSizeW"));};
        auto info=[&](HMODULE m){return reinterpret_cast<Info>(GetProcAddress(m,"GetFileVersionInfoW"));};
        auto query=[&](HMODULE m){return reinterpret_cast<Query>(GetProcAddress(m,"VerQueryValueW"));};
        auto file=std::filesystem::path(system)/L"kernel32.dll";
        DWORD ignored{},n=size(proxy)(file.c_str(),&ignored);
        check(n && n==size(original)(file.c_str(),&ignored),"version size forwarding differs");
        std::vector<unsigned char> a(n),b(n);
        check(info(proxy)(file.c_str(),0,n,a.data()) && info(original)(file.c_str(),0,n,b.data()),"version read forwarding");
        void *pa{},*pb{};UINT na{},nb{};
        check(query(proxy)(a.data(),L"\\",&pa,&na) && query(original)(b.data(),L"\\",&pb,&nb),"version query forwarding");
        check(na==nb && memcmp(pa,pb,na)==0,"version values differ");
        std::cout<<"Checking 64 KiB thread attach/detach notifications.\n"<<std::flush;
        for(int i=0;i<8;++i) {
            HANDLE thread=CreateThread(nullptr,64*1024,small_thread,nullptr,STACK_SIZE_PARAM_IS_A_RESERVATION,nullptr);
            check(thread!=nullptr,"small-stack thread creation failed");
            check(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0,"small-stack thread timed out");
            DWORD code=1;GetExitCodeThread(thread,&code);CloseHandle(thread);
            check(code==0,"small-stack thread failed");
        }
        std::cout<<"Proxy checks passed: 17 exports, system forwarding, eight small-stack thread lifecycles.\n";
        // The DLL owns a worker thread. Let process exit release it; do not unload it here.
        return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
