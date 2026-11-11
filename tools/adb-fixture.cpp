#include <filesystem>
#include <fstream>
#include <iostream>
#include <windows.h>
int wmain(int argc,wchar_t** argv) {
    std::wstring exe(32768,L'\0');auto n=GetModuleFileNameW(nullptr,exe.data(),DWORD(exe.size()));exe.resize(n);
    auto root=std::filesystem::path(exe).parent_path();
    if(argc>1 && std::wstring_view(argv[1])==L"devices") {
        std::cout<<"List of devices attached\n";
        if(std::filesystem::exists(root/L"online"))std::cout<<"192.0.2.1:5555 device model:Fixture\n";
        return 0;
    }
    if(argc==3 && std::wstring_view(argv[1])==L"connect" && std::wstring_view(argv[2])==L"192.0.2.1:5555") {
        std::ofstream(root/L"online")<<"1";std::ofstream(root/L"connections",std::ios::app)<<"1\n";return 0;
    }
    if(argc==6 && std::wstring_view(argv[3])==L"shell" && std::wstring_view(argv[4])==L"wm") {
        std::cout<<"Physical size: 1280x720\n";return 0;
    }
    return 1;
}
