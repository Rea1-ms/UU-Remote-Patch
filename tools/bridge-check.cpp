#include "session.hpp"
#include "texture.hpp"
#include <iostream>
#include <fstream>
#include <algorithm>
using namespace uu;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static void protocol() {
    std::vector<std::vector<uint8_t>> messages;
    Input input([&](auto& bytes){messages.push_back(bytes);return true;});input.size(1080,1920);
    check(input.event("0:SLOTMULTIPRESS:26:26:100:200:27:27:900:1700\n"),"two fingers rejected");
    check(messages.size()==2,"missing second finger");
    check(messages[0].size()==32 && messages[0][0]==2 && messages[0][1]==0,"invalid touch packet");
    check(be64(messages[0].data()+2)==26 && be64(messages[1].data()+2)==27,"finger identity lost");
    check(be32(messages[0].data()+10)==100 && be32(messages[0].data()+14)==200,"coordinates changed");
    check(messages[0][18]==4 && messages[0][19]==56 && messages[0][20]==7 && messages[0][21]==128,"screen size byte order");
    check(input.event("0:SLOTMULTIPRESS:26:26:200:300:27:27:800:1600\n"),"pinch movement rejected");
    check(messages[2][1]==2 && messages[3][1]==2,"pinch converted to extra presses");
    check(input.event("0:SLOTMULTIRELEASE:26\n"),"release rejected");
    check(messages.back()[1]==1 && be64(messages.back().data()+2)==26,"wrong finger released");
    input.reset();check(messages.size()==6 && be64(messages.back().data()+2)==27,"disconnect left a pressed finger");
    size_t before=messages.size();
    check(!input.event("0:SLOTMULTIPRESS:26:26:1:2:27:oops:3:4\n"),"malformed packet accepted");
    check(messages.size()==before,"partially injected malformed packet");
    for(auto [native,android]:{std::pair{158,4},std::pair{172,3},std::pair{580,187}}) {
        before=messages.size();auto code=std::to_string(native);
        check(input.event("0:KBDPR:"+code+":1\n0:KBDRL:"+code+":0\n"),"official navigation key rejected");
        check(messages.size()==before+2 && be32(messages[before].data()+2)==uint32_t(android) &&
            be32(messages[before+1].data()+2)==uint32_t(android) && messages[before][1]==0 && messages[before+1][1]==1,"navigation key mapping");
    }
    before=messages.size();check(input.system("tab_manage"),"official Home operation rejected");
    check(messages.size()==before+2 && be32(messages[before].data()+2)==3 && messages.back()[1]==1,"Home operation mapping");
    check(!input.system("unknown"),"unknown system operation accepted");
    input.size(6,4);
    const int raw_corners[][2]={{0,0},{3,0},{0,5},{3,5}};
    const int upright_corners[][2]={{0,3},{0,0},{5,3},{5,0}};
    for(int i=0;i<4;++i) {
        auto command="0:SLOTMULTIPRESS:0:0:"+std::to_string(raw_corners[i][0])+":"+std::to_string(raw_corners[i][1])+"\n";
        check(input.event(command),"landscape corner rejected");auto& bytes=messages.back();
        check(be32(bytes.data()+10)==uint32_t(upright_corners[i][0]) && be32(bytes.data()+14)==uint32_t(upright_corners[i][1]) &&
            bytes[19]==6 && bytes[21]==4,"landscape input corner mapping");
    }
    before=messages.size();input.size(4,6);
    check(messages.size()==before+1 && messages.back()[1]==1,"rotation retained a pressed finger");
    check(input.text("abc"),"text rejected");check(messages.back()==std::vector<uint8_t>({1,0,0,0,3,'a','b','c'}),"text encoding");
    check(input.text(std::string(300,'a')),"maximum text rejected");before=messages.size();
    check(!input.text(std::string(301,'a')),"oversized text accepted");
    check(!input.text(std::string("\xc0\xaf",2)),"invalid UTF-8 accepted");
    check(messages.size()==before,"rejected text emitted a packet");
    std::cout<<"Protocol checks passed: multitouch, rotation corners, resize release, navigation keys, Home operation and text.\n";
}
static void pixel_check() {
    // Upright red/green/blue on top; yellow/magenta/cyan below.
    Frame frame{3,2,1,{0,0,255,255, 0,255,0,255, 255,0,0,255,
        0,255,255,255, 255,0,255,255, 255,255,0,255}};
    const std::vector<uint8_t> screenshot={255,0,0,255, 0,255,0,255, 0,0,255,255,
        255,255,0,255, 255,0,255,255, 0,255,255,255};
    const std::vector<uint8_t> portrait={0,255,255,255, 0,0,255,255,
        255,0,255,255, 0,255,0,255, 255,255,0,255, 255,0,0,255};
    check(frame_pixels(frame,3,2,false,true)==screenshot,"screenshot RGBA colors differ");
    check(frame_pixels(frame,2,3,true,false)==portrait,"portrait storage rotation differs");
    auto scaled=frame_pixels(frame,6,4,false,true);
    for(int y=0;y<4;++y)for(int x=0;x<6;++x)
        check(std::equal(scaled.begin()+(y*6+x)*4,scaled.begin()+(y*6+x+1)*4,screenshot.begin()+((y/2)*3+x/2)*4),"screenshot scale differs");
    Frame upright{2,3,1,portrait};check(frame_pixels(upright,2,3,true,false)==portrait,"portrait frame was rotated");
}
static void texture_check(const Frame& frame) {
    using Microsoft::WRL::ComPtr;
    TextureWriter writer;
    std::istringstream info(writer.info(frame.width,frame.height));unsigned w,h,vendor,id;info>>w>>h>>vendor>>id;
    ComPtr<IDXGIFactory1> factory;check(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))),"DXGI factory");
    ComPtr<IDXGIAdapter1> adapter;
    for(UINT i=0;factory->EnumAdapters1(i,&adapter)==S_OK;++i){DXGI_ADAPTER_DESC1 d{};adapter->GetDesc1(&d);if(d.VendorId==vendor && d.DeviceId==id)break;adapter.Reset();}
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    check(SUCCEEDED(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)),"D3D device");
    for(bool nt:{true,false})for(auto format:{DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM}) {
        D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;
        d.Format=format;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
        d.MiscFlags=D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX|(nt?D3D11_RESOURCE_MISC_SHARED_NTHANDLE:0);
        ComPtr<ID3D11Texture2D> texture;check(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&texture)),"shared texture");
        HANDLE handle{};
        if(nt){ComPtr<IDXGIResource1> resource;texture.As(&resource);check(SUCCEEDED(resource->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,nullptr,&handle)),"NT handle");}
        else{ComPtr<IDXGIResource> resource;texture.As(&resource);check(SUCCEEDED(resource->GetSharedHandle(&handle)),"legacy handle");}
        ComPtr<IDXGIKeyedMutex> mutex;texture.As(&mutex);
        for(int i=0;i<8;++i){SharedFrame shared{w,h,0,nt,{},handle,GetCurrentProcessId(),0};
            check(writer.write(shared,frame),"texture writer handshake");check(mutex->AcquireSync(1,1000)==S_OK,"consumer handshake");
            if(i==0) {
                auto staging_desc=d;staging_desc.BindFlags=staging_desc.MiscFlags=0;staging_desc.Usage=D3D11_USAGE_STAGING;staging_desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
                ComPtr<ID3D11Texture2D> staging;check(SUCCEEDED(device->CreateTexture2D(&staging_desc,nullptr,&staging)),"staging texture");
                context->CopyResource(staging.Get(),texture.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
                check(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"read back texture");
                auto expected=frame_pixels(frame,int(w),int(h),true,format==DXGI_FORMAT_R8G8B8A8_UNORM);
                bool same=true;for(UINT y=0;y<h;++y)if(memcmp(static_cast<const uint8_t*>(mapped.pData)+size_t(y)*mapped.RowPitch,expected.data()+size_t(y)*w*4,size_t(w)*4)){same=false;break;}
                context->Unmap(staging.Get(),0);check(same,"shared texture pixels differ");
            }
            check(SUCCEEDED(mutex->ReleaseSync(0)),"consumer release");
        }
        if(nt)CloseHandle(handle);
    }
    std::cout<<"Shared texture checks passed: NT and legacy handles, BGRA/RGBA, rotation, repeated handshake and readback.\n";
}
int wmain(int argc,wchar_t** argv) {
    try {
        set_root(fs::current_path());
        if(argc<2)return 2;
        std::wstring mode=argv[1];
        if(mode==L"protocol"){protocol();return 0;}
        if(mode==L"texture"){pixel_check();Frame frame{320,240,1,{}};frame.pixels.resize(320*240*4);for(size_t i=0;i<frame.pixels.size();++i)frame.pixels[i]=uint8_t(i*31);texture_check(frame);Frame portrait{240,320,1,frame.pixels};texture_check(portrait);return 0;}
        if(mode==L"device") {
            read_config();refresh_devices();auto list=devices();auto d=std::find_if(list.begin(),list.end(),[&](auto& d){return d.online && (argc<3 || d.index==_wtoi(argv[2]));});
            check(d!=list.end(),"no matching online device");
            Session session(*d,[](int w,int h){std::cout<<"video size="<<w<<'x'<<h<<'\n';});
            check(session.start(),"device video connection failed");
            auto first=session.latest();check(first && first->sequence>0,"no decoded frame");
            std::cout<<"decoded instance="<<d->index<<" size="<<first->width<<'x'<<first->height<<'\n';
            texture_check(*first);Sleep(5000);
            auto last=session.latest();std::cout<<"decoded frames="<<last->sequence<<'\n';
            check(session.alive(),"video disconnected during validation");session.stop();return 0;
        }
        return 2;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
