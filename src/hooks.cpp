#include "common.hpp"
#include "texture.hpp"
#include "MinHook.h"
#include "json.hpp"
#include <bcrypt.h>
#include <fstream>
#include <sstream>
#include <map>
#include <algorithm>

namespace uu {
struct NrdCallbacks {
    void (*system)(int,const char*,const char*);
    void (*screenshot)(int,int,int,int,const void*);
    void (*log)(int,const char*,int);
    void (*stream)(int,int,void*,int);
};
static NrdCallbacks callbacks{};
static NrdCallbacks* native_callbacks=nullptr;
static std::mutex callbacks_mutex, channels_mutex;
struct Channel {
    int index=0,id=0;
    std::shared_ptr<Session> session;
    bool connecting=false,closed=false;
    int reported_width=0,reported_height=0;
    TextureWriter texture;
    std::mutex lifecycle;
};
static std::map<int,std::shared_ptr<Channel>> channels;
static std::atomic<uint32_t> next_channel{0x40000000};
static std::mutex geometry_mutex;
static std::map<int,Geometry> geometries;
static void remember_geometry(int index,int width,int height){std::lock_guard lock(geometry_mutex);geometries[index]={width,height};}
static Geometry geometry_for(const Device& d){std::lock_guard lock(geometry_mutex);auto it=geometries.find(d.index);return it==geometries.end()?Geometry{d.width,d.height}:it->second;}
static bool virtual_handle(int id){return id>=0x40000000;}
static std::shared_ptr<Session> session(const std::shared_ptr<Channel>& c){std::lock_guard lock(c->lifecycle);return c->closed?nullptr:c->session;}
static NrdCallbacks get_callbacks(){std::lock_guard lock(callbacks_mutex);return callbacks.system?callbacks:(native_callbacks?*native_callbacks:NrdCallbacks{});}
static std::shared_ptr<Channel> channel(int id){std::lock_guard lock(channels_mutex);auto it=channels.find(id);return it==channels.end()?nullptr:it->second;}
static void notify(const std::shared_ptr<Channel>& c,int width,int height) {
    {std::lock_guard lock(c->lifecycle);if(c->closed)return;c->reported_width=width;c->reported_height=height;}
    auto cb=get_callbacks();if(!cb.system)return;
    Geometry geometry{width,height};
    auto display="0 "+std::to_string(geometry.raw_width())+" "+std::to_string(geometry.raw_height())+" "+std::to_string(geometry.rotation());
    auto info=c->texture.info(width,height);
    cb.system(c->id,"active_display",display.c_str());
    cb.system(c->id,"renderer_info",info.c_str());
}
static bool is_virtual(int index){return index>=10000 && index<20000 && device(index).has_value();}
static int copy_value(std::string_view value,char* output,int capacity) {
    if(!output || capacity<=0 || value.size()>=size_t(capacity))return -2;
    std::copy(value.begin(),value.end(),output);output[value.size()]=0;return 0;
}
static std::string setting(int index,std::string_view key) {
    auto d=device(index);if(!d)return {};
    if(key=="player_name")return d->name;
    if(key=="renderer_mode")return "dx";
    if(key=="resolution_mode")return "custom";
    if(key=="frame_rate" || key=="max_frame_rate")return std::to_string(config().fps);
    if(key=="resolution_width.custom"){auto g=geometry_for(*d);return g.width>0?std::to_string(g.raw_width()):"";}
    if(key=="resolution_height.custom"){auto g=geometry_for(*d);return g.height>0?std::to_string(g.raw_height()):"";}
    if(key=="resolution_dpi.custom")return "240";
    if(key=="performance_mode")return "custom";
    if(key=="performance_cpu.custom")return "4";
    if(key=="performance_mem.custom")return "4";
    return {};
}
struct Manager {
    int (*list)(int*,int*){};
    int (*status)(int){};
    int (*get)(int,const char*,char*,int){};
    int (*batch)(int,const char*,char*,int){};
    int (*launch)(int){};
    int (*launch_params)(int,const char*){};
    int (*shutdown)(int){};
    int (*connect)(int){};
    int (*disconnect)(int){};
    int (*last_error)(int,int*,char*,int){};
};
static Manager manager;
static thread_local std::vector<int> pending_list;
static int list(int* output,int* count) {
    if(!count)return -1;
    log(std::string("instance list request ")+(output?"data":"count"));
    if(!output || pending_list.empty()) {
        int n=0;int result=manager.list(nullptr,&n);
        if(result || n<0 || n>1024)return result?result:-1;
        pending_list.resize(n);
        int available=n;
        if(n && manager.list(pending_list.data(),&n)){pending_list.clear();return -1;}
        if(n<0 || n>available){pending_list.clear();return -1;}
        pending_list.resize(n);
        for(auto& d:devices())if(d.online && std::find(pending_list.begin(),pending_list.end(),d.index)==pending_list.end())pending_list.push_back(d.index);
    }
    int capacity=*count;*count=int(pending_list.size());
    if(!output)return 0;
    if(capacity<*count)return -2;
    std::copy(pending_list.begin(),pending_list.end(),output);pending_list.clear();return 0;
}
static int status(int index) {
    if(!is_virtual(index))return manager.status(index);
    auto d=device(index);return d && d->online?3:0;
}
static int get(int index,const char* key,char* output,int capacity) {
    if(!is_virtual(index))return manager.get(index,key,output,capacity);
    if(!key)return -1;
    auto value=setting(index,key);
    log("setting instance "+std::to_string(index)+" key="+key);
    return copy_value(value,output,capacity);
}
static int batch(int index,const char* keys,char* output,int capacity) {
    if(!is_virtual(index))return manager.batch(index,keys,output,capacity);
    try {
        nlohmann::json out={{"errcode",0},{"errmsg",""}};
        auto parsed=nlohmann::json::parse(keys?keys:"",nullptr,false);
        if(parsed.is_array()){for(auto& k:parsed)if(k.is_string())out[k.get<std::string>()]=setting(index,k.get<std::string>());}
        else if(parsed.is_object()){for(auto& [key,value]:parsed.items()){(void)value;out[key]=setting(index,key);}}
        else {std::istringstream stream(keys?keys:"");std::string key;while(std::getline(stream,key,','))out[key]=setting(index,key);}
        return copy_value(out.dump(),output,capacity);
    }catch(...){return -1;}
}
static int launch(int index){return is_virtual(index)?(device(index)->online?0:-1):manager.launch(index);}
static int launch_params(int index,const char* params){return is_virtual(index)?launch(index):manager.launch_params(index,params);}
static int shutdown_player(int index){if(!is_virtual(index))return manager.shutdown(index);return 0;}
static int connect_player(int index){return is_virtual(index)?0:manager.connect(index);}
static int disconnect_player(int index){return is_virtual(index)?0:manager.disconnect(index);}
static int last_error(int index,int* code,char* out,int size){if(!is_virtual(index))return manager.last_error(index,code,out,size);if(code)*code=0;return copy_value("",out,size);}

struct StringAbi {char data[16];size_t size,capacity;};
static_assert(sizeof(StringAbi)==32);
using Instance=StringAbi*(*)(StringAbi*,void*,unsigned,bool*);
static Instance original_instance{};
static StringAbi* instance(StringAbi* out,void* context,unsigned index,bool* export_mode) {
    if(!is_virtual(int(index)))return original_instance(out,context,index,export_mode);
    *out={};auto name="uadb"+std::to_string(index);
    std::copy(name.begin(),name.end(),out->data);out->size=name.size();out->capacity=15;
    if(export_mode)*export_mode=true;
    log("renderer instance requested "+std::to_string(index));
    return out;
}
static void (*original_set)(NrdCallbacks){};
static int (*original_create)(const char*){};
static int (*original_connect)(int){};
static void (*original_destroy)(int){};
static int (*original_send)(int,int,void*,int){};
static int (*original_event)(int,const char*,int){};
static int (*original_text)(int,const char*){};
static int (*original_operate)(int,const char*,const char*){};
static int (*original_screenshot)(int,int,int){};
static void set_callbacks(NrdCallbacks cb){{std::lock_guard lock(callbacks_mutex);callbacks=cb;}original_set(cb);}
static int create(const char* name) {
    if(!name || !std::string_view(name).starts_with("uadb"))return original_create(name);
    try {
        int index=std::stoi(name+4);if(!is_virtual(index))return -1;
        auto c=std::make_shared<Channel>();c->index=index;
        auto id=next_channel.fetch_add(1);if(id>=0x7fffffff)return -1;c->id=int(id);
        std::lock_guard lock(channels_mutex);channels.emplace(c->id,c);
        log("created virtual renderer "+std::to_string(index));return c->id;
    }catch(...){return -1;}
}
static int connect(int id) {
    auto c=channel(id);if(!c)return virtual_handle(id)?-1:original_connect(id);
    std::shared_ptr<Session> started,retired;
    try {
        auto d=device(c->index);if(!d || !d->online)return -1;
        {
            std::lock_guard lock(c->lifecycle);
            if(c->closed || c->connecting)return -1;
            if(c->session && c->session->alive())return 0;
            // Decoder callbacks update only local metadata. Host callbacks run on
            // request threads, so a reentrant destroy can safely join the reader.
            started=std::make_shared<Session>(*d,[index=c->index](int w,int h){remember_geometry(index,w,h);});
            retired=std::move(c->session);c->session=started;c->connecting=true;
        }
        retired.reset(); // Stop old readers without holding a callback's lifecycle lock.
        bool ok=started->start();
        {
            std::lock_guard lock(c->lifecycle);c->connecting=false;
            if(!ok || c->closed){if(c->session==started)c->session.reset();return -1;}
        }
        auto frame=started->latest();if(frame)notify(c,frame->width,frame->height);
        log("connected virtual renderer "+std::to_string(c->index));return 0;
    }catch(const std::exception& e){
        {std::lock_guard lock(c->lifecycle);c->connecting=false;if(c->session==started)c->session.reset();}
        log(e.what());return -1;
    }
}
static void destroy(int id) {
    auto c=channel(id);if(!c){if(!virtual_handle(id))original_destroy(id);return;}
    {std::lock_guard lock(channels_mutex);channels.erase(id);}
    std::shared_ptr<Session> retired;
    {std::lock_guard lock(c->lifecycle);c->closed=true;retired=std::move(c->session);}
    // Joining a reader must not hold the lock that a host callback may reenter.
    retired.reset();
    log("destroyed virtual renderer "+std::to_string(c->index));
}
static int send(int id,int type,void* data,int size) {
    auto c=channel(id);if(!c)return virtual_handle(id)?-1:original_send(id,type,data,size);
    if(type!=1 || !data || size!=sizeof(SharedFrame))return -1;
    try {
        auto s=session(c);if(!s || !s->alive())return -1;
        auto frame=s->latest();if(!frame)return -1;
        bool changed;
        {std::lock_guard lock(c->lifecycle);changed=c->reported_width!=frame->width || c->reported_height!=frame->height;}
        if(changed){notify(c,frame->width,frame->height);return 1;}
        return c->texture.write(*static_cast<SharedFrame*>(data),*frame)?0:-1;
    }catch(...){return -1;}
}
static int event(int id,const char* data,int size) {
    auto c=channel(id);if(!c)return virtual_handle(id)?-1:original_event(id,data,size);
    if(!data || size<0 || size>16384)return -1;
    auto s=session(c);bool ok=s && s->event(std::string_view(data,size));
    if(!ok)log("unsupported or disconnected input for instance "+std::to_string(c->index));
    return ok?0:-1;
}
static int text(int id,const char* data) {
    auto c=channel(id);if(!c)return virtual_handle(id)?-1:original_text(id,data);
    if(!data)return -1;
    auto s=session(c);return s && s->text(data)?0:-1;
}
static int operate(int id,const char* name,const char* data) {
    auto c=channel(id);if(!c)return virtual_handle(id)?-1:original_operate(id,name,data);
    if(!name)return -1;
    if(std::string_view(name)=="renderer_info" || std::string_view(name)=="active_display") {
        auto s=session(c);auto frame=s?s->latest():nullptr;
        if(frame)notify(c,frame->width,frame->height);return frame?0:-1;
    }
    if(std::string_view(name)=="tab_manage"){auto s=session(c);return s && s->system(name)?0:-1;}
    log("unsupported system operation "+std::string(name));return -1;
}
static int screenshot(int id,int width,int height) {
    auto c=channel(id);if(!c)return virtual_handle(id)?-1:original_screenshot(id,width,height);
    if(width<1 || height<1 || width>4096 || height>4096)return -1;
    auto s=session(c);auto frame=s?s->latest():nullptr;
    auto cb=get_callbacks();if(!frame || !cb.screenshot)return -1;
    auto pixels=frame_pixels(*frame,width,height,false,true);
    cb.screenshot(id,width,height,int(pixels.size()),pixels.data());return 0;
}
template<class T> static void hook(void* address,T detour,T& original) {
    if(!address || MH_CreateHook(address,reinterpret_cast<void*>(detour),reinterpret_cast<void**>(&original))!=MH_OK)
        throw std::runtime_error("hook creation failed");
}
template<class T> static void exported(HMODULE lib,const char* name,T detour,T& original) {
    hook(reinterpret_cast<void*>(GetProcAddress(lib,name)),detour,original);
}
static std::string hash(const fs::path& file) {
    std::ifstream in(file,std::ios::binary);if(!in)return {};
    BCRYPT_ALG_HANDLE alg{};BCRYPT_HASH_HANDLE h{};
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return {};
    if(BCryptCreateHash(alg,&h,nullptr,0,nullptr,0,0)<0){BCryptCloseAlgorithmProvider(alg,0);return {};}
    std::vector<char> buf(65536);bool ok=true;
    while(in){in.read(buf.data(),std::streamsize(buf.size()));if(BCryptHashData(h,reinterpret_cast<PUCHAR>(buf.data()),ULONG(in.gcount()),0)<0){ok=false;break;}}
    unsigned char digest[32]{};if(BCryptFinishHash(h,digest,sizeof(digest),0)<0)ok=false;
    BCryptDestroyHash(h);BCryptCloseAlgorithmProvider(alg,0);if(!ok)return {};
    constexpr char chars[]="0123456789abcdef";std::string out;
    for(auto b:digest){out+=chars[b>>4];out+=chars[b&15];}return out;
}
static void install_hooks() {
    const auto bin=module_folder();
    const std::pair<const wchar_t*,const char*> supported[]={
        {L"GameViewerServer.exe","91cfe3bfc5582776a56a2cf6294c45afb7d65d68abe8c4a483e99a8549b00b5b"},
        {L"nrd_renderer_ipc.dll","d3960b8b14ec0020b89ccbc51f7414cdbfa67897aa5fa29b2014ed8e84e6aa1a"},
        {L"nemu-api.dll","40f44159d6fd637e2226a133508d032470e5caf22f35ef1065c86438fdd90abb"}};
    for(auto& [name,expected]:supported)if(hash(bin/name)!=expected)throw std::runtime_error("unsupported host build; no hooks installed");
    auto management=LoadLibraryW((bin/L"nemu-api.dll").c_str());
    auto nrd=GetModuleHandleW(L"nrd_renderer_ipc.dll");
    if(!management || !nrd)throw std::runtime_error("host modules unavailable");
    native_callbacks=reinterpret_cast<NrdCallbacks*>(reinterpret_cast<uint8_t*>(nrd)+0x29e48);
    if(MH_Initialize()!=MH_OK)throw std::runtime_error("hook engine unavailable");
    exported(management,"getPlayerList",list,manager.list);
    exported(management,"getPlayerStatus",status,manager.status);
    exported(management,"getPlayerLocalSetting",get,manager.get);
    exported(management,"getPlayerLocalSettingForBatch",batch,manager.batch);
    exported(management,"launchPlayer",launch,manager.launch);
    exported(management,"launchPlayerWithParams",launch_params,manager.launch_params);
    exported(management,"shutdownPlayer",shutdown_player,manager.shutdown);
    exported(management,"connectPlayer",connect_player,manager.connect);
    exported(management,"disconnectPlayer",disconnect_player,manager.disconnect);
    exported(management,"getPlayerLastLaunchError",last_error,manager.last_error);
    auto host=reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    hook(host+0x4eef70,instance,original_instance);
    exported(nrd,"?nrd_set_callback@@YAXUnrd_callback@@@Z",set_callbacks,original_set);
    exported(nrd,"?nrd_ipc_create@@YAHPEBD@Z",create,original_create);
    exported(nrd,"?nrd_ipc_connect@@YAHH@Z",connect,original_connect);
    exported(nrd,"?nrd_ipc_destroy@@YAXH@Z",destroy,original_destroy);
    exported(nrd,"?nrd_ipc_send_streamer_data@@YAHHHPEAXH@Z",send,original_send);
    exported(nrd,"?nrd_input_send_event@@YAHHPEBDH@Z",event,original_event);
    exported(nrd,"?nrd_input_send_text@@YAHHPEBD@Z",text,original_text);
    exported(nrd,"?nrd_system_operate@@YAHHPEBD0@Z",operate,original_operate);
    exported(nrd,"?nrd_screenshot@@YAHHHH@Z",screenshot,original_screenshot);
    if(MH_EnableHook(MH_ALL_HOOKS)!=MH_OK)throw std::runtime_error("hook activation failed");
    log("hooks active for host 4.46.0.4716");
}
#ifdef UU_PATCH_CONTRACT_CHECK
static std::atomic<bool> check_callback_ok{true};
static std::atomic<int> check_screenshots{0};
static void check_system(int id,const char* name,const char* data) {
    auto c=channel(id);auto s=c?session(c):nullptr;auto frame=s?s->latest():nullptr;
    if(!frame){check_callback_ok=false;return;}
    auto geometry=frame->geometry();
    if(std::string_view(name)=="active_display") {
        std::istringstream values(data);int display,w,h,rotation;
        if(!(values>>display>>w>>h>>rotation) || display!=0 || w!=geometry.raw_width() ||
           h!=geometry.raw_height() || rotation!=geometry.rotation())check_callback_ok=false;
    }
}
static void check_screenshot(int id,int w,int h,int bytes,const void* pixels) {
    if(!channel(id) || w!=320 || h!=180 || bytes!=w*h*4 || !pixels)check_callback_ok=false;
    ++check_screenshots;
}
int check_management(const fs::path& library,const fs::path& installation,bool video) {
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    auto lib=LoadLibraryExW(library.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    require(lib!=nullptr,"management library unavailable");
    auto init=reinterpret_cast<void(*)(const wchar_t*)>(GetProcAddress(lib,"init"));
    auto uninit=reinterpret_cast<void(*)()>(GetProcAddress(lib,"uninit"));
    manager.list=reinterpret_cast<decltype(manager.list)>(GetProcAddress(lib,"getPlayerList"));
    manager.status=reinterpret_cast<decltype(manager.status)>(GetProcAddress(lib,"getPlayerStatus"));
    manager.get=reinterpret_cast<decltype(manager.get)>(GetProcAddress(lib,"getPlayerLocalSetting"));
    require(init && uninit && manager.list && manager.status && manager.get,"management exports unavailable");
    init(installation.c_str());
    int original_count=0;require(manager.list(nullptr,&original_count)==0,"original list count failed");
    std::vector<int> original(original_count);int actual=original_count;
    require(manager.list(original.data(),&actual)==0 && actual==original_count,"original list changed");
    auto found=devices();auto online=std::count_if(found.begin(),found.end(),[](auto& d){return d.online;});
    require(online>0,"no online device for management check");
    int count=0;require(list(nullptr,&count)==0 && count==original_count+online,"virtual list count differs");
    std::vector<int> result(count);int short_capacity=count-1;
    require(list(result.data(),&short_capacity)==-2 && short_capacity==count,"list capacity guard failed");
    int capacity=count;require(list(result.data(),&capacity)==0 && capacity==count,"virtual list failed");
    require(std::equal(original.begin(),original.end(),result.begin()),"original instances changed");
    for(auto index:original){char a[512]{},b[512]{};
        require(get(index,"player_name",a,sizeof(a))==manager.get(index,"player_name",b,sizeof(b)) && !strcmp(a,b),"original setting fallback differs");
        require(status(index)==manager.status(index),"original status fallback differs");
    }
    for(auto& d:found)if(d.online){char name[512]{};StringAbi out{};bool exported=false;
        require(std::count(result.begin(),result.end(),d.index)==1,"virtual index missing or duplicated");
        require(get(d.index,"player_name",name,sizeof(name))==0 && name==d.name,"virtual name differs");
        require(instance(&out,nullptr,unsigned(d.index),&exported)==&out && out.size<16 && out.capacity==15 && exported,"short string layout differs");
        auto id=create(out.data),second=create(out.data);
        require(id>=0 && second>=0 && id!=second,"same-device renderer handles collided");
        require(screenshot(id,320,240)==-1,"unconnected screenshot should fail");destroy(id);
        require(!channel(id) && channel(second)!=nullptr,"destroy removed the other renderer");
        require(connect(id)==-1 && event(id,"",0)==-1 && operate(id,"renderer_info","")==-1,"retired virtual handle reached native renderer");
        destroy(id);destroy(second);
    }
    if(video) {
        callbacks={check_system,check_screenshot,nullptr,nullptr};
        auto d=std::find_if(found.begin(),found.end(),[](auto& d){return d.online;});
        auto name="uadb"+std::to_string(d->index);
        for(int attempt=0;attempt<3;++attempt) {
            int first=create(name.c_str()),second=create(name.c_str()),a=-1,b=-1;
            require(first>=0 && second>=0 && first!=second,"parallel renderer creation failed");
            std::thread one([&]{a=connect(first);}),two([&]{b=connect(second);});one.join();two.join();
            require(a==0 && b==0,"parallel video connection failed");
            require(screenshot(first,320,180)==0 && screenshot(second,320,180)==0,"parallel preview failed");
            destroy(first);
            require(operate(second,"renderer_info","")==0 && screenshot(second,320,180)==0,"destroy interrupted another channel");
            require(check_callback_ok && check_screenshots==(attempt+1)*3,"host callback geometry or reentry failed");
            destroy(second);
        }
        callbacks={};
    }
    uninit();return int(online);
}
#endif
}
DWORD WINAPI patch_worker(void* module) {
    try {
        std::wstring exe(32768,L'\0');auto length=GetModuleFileNameW(nullptr,exe.data(),DWORD(exe.size()));
        if(!length || length>=exe.size())return 0;exe.resize(length);
        if(_wcsicmp(uu::fs::path(exe).filename().c_str(),L"GameViewerServer.exe"))return 0;
        auto folder=uu::patch_folder(uu::module_folder(static_cast<HMODULE>(module)));
        uu::set_root(folder);uu::log("patch loading");
        // Pin the module: detours and background tasks must remain valid until host exit.
        HMODULE pinned{};GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&patch_worker),&pinned);
        uu::read_config();uu::install_hooks();
        // Start even when Android/ADB is not ready at system boot.
        for(;;){try{uu::refresh_devices();}catch(...){uu::log("discovery refresh failed");}Sleep(5000);}
    }catch(const std::exception& e){uu::log(e.what());}
    return 0;
}
