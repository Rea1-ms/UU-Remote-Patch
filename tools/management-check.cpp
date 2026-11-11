#include "common.hpp"
#include <iostream>
namespace uu { int check_management(const fs::path&,const fs::path&,bool); }
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc<3 || argc>4 || (argc==4 && std::wstring_view(argv[3])!=L"video"))return 2;
        uu::set_root(uu::fs::current_path());uu::read_config();uu::refresh_devices();
        auto n=uu::check_management(uu::fs::canonical(argv[1]),uu::fs::canonical(argv[2]),argc==4);
        std::cout<<"Management checks passed: original fallback, buffer capacity, "<<n<<" virtual instances, short strings, independent same-device handles and retired handles.\n";
        if(argc==4)std::cout<<"Parallel video checks passed: three connection cycles, screenshots, callback reentry and independent destruction; no input injected.\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
