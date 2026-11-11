#include "common.hpp"
#include "json.hpp"
#include <fstream>
#include <iostream>
using namespace uu;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static void fixture(const fs::path& folder,const fs::path& adb) {
    fs::create_directories(folder/L"runtime/scrcpy");
    fs::copy_file(adb,folder/L"runtime/scrcpy/adb.exe");
    for(auto name:{L"scrcpy-server",L"avcodec-63.dll",L"avutil-61.dll"})std::ofstream(folder/L"runtime/scrcpy"/name)<<"fixture";
    nlohmann::json j={{"adb","runtime/scrcpy/adb.exe"},{"scrcpy","runtime/scrcpy"},
        {"connect",{"192.0.2.1:5555"}},{"fps",100},{"max_size",1},{"bit_rate",1}};
    std::ofstream(folder/L"config.local.json")<<j.dump(2);
}
int wmain(int argc,wchar_t** argv) {
    try {
        check(argc==2,"supply fake ADB executable");
        auto test=fs::temp_directory_path()/(L"uu-config-check-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        auto first=test/L"original",moved=test/L"moved 中文 空格";
        auto adb=fs::absolute(argv[1]);fixture(first,adb);fixture(moved,adb);
        fs::current_path(fs::temp_directory_path());
        for(auto& folder:{first,moved}) {
            set_root(folder);read_config();
            check(config().adb==folder/L"runtime/scrcpy/adb.exe","ADB path depends on working directory");
            check(config().scrcpy==folder/L"runtime/scrcpy","decoder path depends on working directory");
            check(config().fps==60 && config().max_size==320 && config().bit_rate==1000000,"video bounds differ");
            refresh_devices();auto found=devices();
            check(found.size()==1 && found[0].online && found[0].width==1280,"network endpoint did not reconnect");
            refresh_devices();std::ifstream count(folder/L"runtime/scrcpy/connections");
            std::string one,two;check(bool(std::getline(count,one)) && !std::getline(count,two),"online endpoint reconnected unnecessarily");
            fs::rename(folder/L"runtime/scrcpy/online",folder/L"runtime/scrcpy/online.previous");
            refresh_devices();check(devices().size()==1 && devices()[0].online,"reconnect after ADB restart duplicated device");
            std::ifstream retried(folder/L"runtime/scrcpy/connections");
            check(bool(std::getline(retried,one)) && bool(std::getline(retried,two)) && !std::getline(retried,one),"ADB restart did not trigger exactly one reconnect");
        }
        auto installed=test/L"host/uu-adb-patch";fixture(installed,adb);
        check(patch_folder(installed.parent_path())==installed,"installed payload was not selected");
        auto dev=test/L"developer";fs::create_directory(dev);
        std::ofstream(dev/L"patch-root.txt")<<"../original\n";
        check(patch_folder(dev)==first,"relative developer override differs");
        set_root(test/L"missing");bool rejected=false;try{read_config();}catch(...){rejected=true;}
        check(rejected,"missing config accepted");
        std::cout<<"Config relocation, Unicode paths, installed layout and fake-ADB reconnect checks passed. Fixtures retained at "<<utf8(test.wstring())<<'\n';
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
