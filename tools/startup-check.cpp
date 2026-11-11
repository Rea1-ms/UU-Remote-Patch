#include <windows.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>
int wmain() {
    try {
        // Link to version.lib: Windows must resolve this import before main.
        wchar_t exe[32768]{},loaded[32768]{};
        if(!GetModuleFileNameW(nullptr,exe,DWORD(std::size(exe))))throw std::runtime_error("executable path");
        auto module=GetModuleHandleW(L"version.dll");
        if(!module || !GetModuleFileNameW(module,loaded,DWORD(std::size(loaded))))throw std::runtime_error("version import missing");
        auto expected=std::filesystem::path(exe).parent_path()/L"version.dll";
        if(!std::filesystem::equivalent(expected,loaded))throw std::runtime_error("system proxy was not automatically loaded");
        wchar_t system[MAX_PATH]{};GetSystemDirectoryW(system,MAX_PATH);
        auto file=std::filesystem::path(system)/L"kernel32.dll";
        DWORD ignored{},size=GetFileVersionInfoSizeW(file.c_str(),&ignored);
        if(!size)throw std::runtime_error("imported version size failed");
        std::vector<unsigned char> data(size);
        if(!GetFileVersionInfoW(file.c_str(),0,size,data.data()))throw std::runtime_error("imported version read failed");
        void* value{};UINT bytes{};
        if(!VerQueryValueW(data.data(),L"\\",&value,&bytes) || bytes<sizeof(VS_FIXEDFILEINFO))
            throw std::runtime_error("imported version query failed");
        std::cout<<"Application-directory proxy loaded automatically; three imported version APIs passed.\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
