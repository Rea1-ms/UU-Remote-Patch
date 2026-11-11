#include "common.hpp"
#include <iostream>
namespace uu { int check_management(const fs::path&,const fs::path&); }
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc!=3)return 2;
        uu::set_root(uu::fs::current_path());uu::read_config();uu::refresh_devices();
        auto n=uu::check_management(uu::fs::canonical(argv[1]),uu::fs::canonical(argv[2]));
        std::cout<<"Management checks passed: original fallback, buffer capacity, "<<n<<" virtual instances, short strings and renderer lifecycle.\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
