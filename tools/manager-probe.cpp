#include <windows.h>
#include <cstdio>
#include <vector>
int wmain(int argc,wchar_t** argv) {
    if(argc!=3) return 2;
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    auto lib=LoadLibraryExW(argv[1],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!lib) { std::printf("load=%lu\n",GetLastError());return 1; }
    auto init=reinterpret_cast<void(*)(const wchar_t*)>(GetProcAddress(lib,"init"));
    auto list=reinterpret_cast<int(*)(int*,int*)>(GetProcAddress(lib,"getPlayerList"));
    auto status=reinterpret_cast<int(*)(int)>(GetProcAddress(lib,"getPlayerStatus"));
    auto setting=reinterpret_cast<int(*)(int,const char*,char*,int)>(GetProcAddress(lib,"getPlayerLocalSetting"));
    auto batch=reinterpret_cast<int(*)(int,const char*,char*,int)>(GetProcAddress(lib,"getPlayerLocalSettingForBatch"));
    if(!init||!list||!status||!setting||!batch) return 2;
    init(argv[2]); int count=0;int result=list(nullptr,&count);
    std::printf("list result=%d count=%d\n",result,count);
    if(count<0||count>64) return 1;
    std::vector<int> indices(count); result=list(indices.data(),&count);
    for(int index:indices) {
        std::printf("index=%d status=%d\n",index,status(index));
        for(auto key:{"name","player_name","window_size","display_width","display_height"}) {
            char buffer[4096]{};result=setting(index,key,buffer,sizeof(buffer));
            std::printf("setting %s result=%d value=%s\n",key,result,buffer);
        }
        char buffer[4096]{};result=batch(index,"[\"name\",\"player_name\"]",buffer,sizeof(buffer));
        std::printf("batch result=%d value=%s\n",result,buffer);
    }
}
