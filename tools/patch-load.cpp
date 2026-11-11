#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <fstream>
namespace fs=std::filesystem;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(std::string(message)+" (Windows error "+std::to_string(GetLastError())+")");}
int wmain(int argc,wchar_t** argv) {
    std::ofstream report(fs::path(argv[0]).parent_path()/L"load-result.txt");
    try {
        if(argc!=2){std::cerr<<"patch_load PATH_TO_VERSION_DLL\n";return 2;}
        auto dll=fs::canonical(argv[1]);check(dll.filename()==L"version.dll","unexpected library name");
        HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);check(snapshot!=INVALID_HANDLE_VALUE,"process snapshot failed");
        PROCESSENTRY32W entry{sizeof(entry)};DWORD pid=0;
        for(BOOL more=Process32FirstW(snapshot,&entry);more;more=Process32NextW(snapshot,&entry))
            if(!_wcsicmp(entry.szExeFile,L"GameViewerServer.exe")){check(pid==0,"multiple host processes; select the host first");pid=entry.th32ProcessID;}
        CloseHandle(snapshot);check(pid!=0,"host process not running");
        HANDLE token{};
        if(OpenProcessToken(GetCurrentProcess(),TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY,&token)) {
            TOKEN_PRIVILEGES privileges{};privileges.PrivilegeCount=1;
            if(LookupPrivilegeValueW(nullptr,SE_DEBUG_NAME,&privileges.Privileges[0].Luid)) {
                privileges.Privileges[0].Attributes=SE_PRIVILEGE_ENABLED;
                AdjustTokenPrivileges(token,FALSE,&privileges,0,nullptr,nullptr);
            }
            CloseHandle(token);
        }
        HANDLE process=OpenProcess(PROCESS_CREATE_THREAD|PROCESS_VM_OPERATION|PROCESS_VM_WRITE|PROCESS_VM_READ|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
        check(process!=nullptr,"cannot open host process");
        wchar_t image[32768]{};DWORD size=DWORD(std::size(image));
        check(QueryFullProcessImageNameW(process,0,image,&size) && fs::path(image).parent_path()==fs::path(L"C:/Program Files/Netease/GameViewer/bin"),"unexpected host installation");
        HMODULE local_owner{};auto load=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW");
        check(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(load),&local_owner)!=0,"loader owner");
        wchar_t owner_path[32768]{};GetModuleFileNameW(local_owner,owner_path,DWORD(std::size(owner_path)));
        auto owner_name=fs::path(owner_path).filename();uintptr_t remote_owner=0;
        snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);check(snapshot!=INVALID_HANDLE_VALUE,"module snapshot failed");
        MODULEENTRY32W module{sizeof(module)};
        for(BOOL more=Module32FirstW(snapshot,&module);more;more=Module32NextW(snapshot,&module)) {
            if(!_wcsicmp(module.szModule,owner_name.c_str()))remote_owner=reinterpret_cast<uintptr_t>(module.modBaseAddr);
            if(!_wcsicmp(module.szExePath,dll.c_str())){CloseHandle(snapshot);CloseHandle(process);std::cout<<"Patch already loaded.\n";report<<"Patch already loaded.\n";return 0;}
        }
        CloseHandle(snapshot);check(remote_owner!=0,"remote loader module unavailable");
        auto loader=reinterpret_cast<LPTHREAD_START_ROUTINE>(remote_owner+reinterpret_cast<uintptr_t>(load)-reinterpret_cast<uintptr_t>(local_owner));
        size_t bytes=(dll.wstring().size()+1)*sizeof(wchar_t);
        void* memory=VirtualAllocEx(process,nullptr,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);check(memory!=nullptr,"remote allocation failed");
        check(WriteProcessMemory(process,memory,dll.c_str(),bytes,nullptr)!=0,"library path write failed");
        HANDLE thread=CreateRemoteThread(process,nullptr,0,loader,memory,0,nullptr);check(thread!=nullptr,"library load thread failed");
        DWORD wait=WaitForSingleObject(thread,15000),result=0;
        if(wait==WAIT_OBJECT_0){GetExitCodeThread(thread,&result);VirtualFreeEx(process,memory,0,MEM_RELEASE);}
        CloseHandle(thread);CloseHandle(process);
        check(wait==WAIT_OBJECT_0 && result!=0,"library loading failed or timed out");
        std::cout<<"Patch library loaded in host pid="<<pid<<"; check the patch log for activation.\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';report<<e.what()<<'\n';return 1;}
}
