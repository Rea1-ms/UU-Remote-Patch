#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

struct NrdCallbacks {
    void (*system)(int, const char*, const char*);
    void (*screenshot)(int, int, int, int, const void*);
    void (*log)(int, const char*);
    void (*stream)(int, int, void*, int);
};
static void system_event(int id, const char* name, const char* data) {
    std::printf("system handle=%d name=%s data=%s\n", id, name ? name : "", data ? data : "");
    std::fflush(stdout);
}
static void screenshot(int id, int w, int h, int size, const void*) {
    std::printf("screenshot handle=%d %dx%d bytes=%d\n", id, w, h, size);
}
static void log_event(int level, const char* message) { std::printf("log level=%d %s\n", level, message ? message : ""); }
static void stream(int id, int type, void* data, int size) {
    std::printf("stream handle=%d type=%d bytes=%d", id, type, size);
    for (int i = 0; data && i < size && i < 64; ++i)
        std::printf(" %02x", static_cast<unsigned char*>(data)[i]);
    std::puts("");
    std::fflush(stdout);
}
template<class T> T symbol(HMODULE module, const char* name) {
    auto result = reinterpret_cast<T>(GetProcAddress(module, name));
    if (!result) { std::fprintf(stderr, "missing export: %s\n", name); std::exit(2); }
    return result;
}
int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { std::puts("nrd_probe DLL INSTANCE [texture]"); return 2; }
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    HMODULE lib = LoadLibraryExW(argv[1], nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!lib) { std::printf("load error=%lu\n", GetLastError()); return 1; }
    auto set = symbol<void(*)(NrdCallbacks)>(lib, "?nrd_set_callback@@YAXUnrd_callback@@@Z");
    auto create = symbol<int(*)(const char*)>(lib, "?nrd_ipc_create@@YAHPEBD@Z");
    auto connect = symbol<int(*)(int)>(lib, "?nrd_ipc_connect@@YAHH@Z");
    auto destroy = symbol<void(*)(int)>(lib, "?nrd_ipc_destroy@@YAXH@Z");
    auto operate = symbol<int(*)(int,const char*,const char*)>(lib, "?nrd_system_operate@@YAHHPEBD0@Z");
    auto send = symbol<int(*)(int,int,void*,int)>(lib, "?nrd_ipc_send_streamer_data@@YAHHHPEAXH@Z");
    char instance[512]{};
    WideCharToMultiByte(CP_UTF8, 0, argv[2], -1, instance, sizeof(instance), nullptr, nullptr);
    set({system_event, screenshot, log_event, stream});
    int id = create(instance);
    std::printf("created=%d\n", id);
    if (id < 0) return 1;
    int result = connect(id);
    std::printf("connected=%d\n", result);
    std::fflush(stdout);
    if (result == 0) {
        std::printf("renderer_info request=%d\n", operate(id, "renderer_info", ""));
        Sleep(1000);
        if (argc > 3) {
            ComPtr<IDXGIFactory1> factory;
            CreateDXGIFactory1(IID_PPV_ARGS(&factory));
            ComPtr<IDXGIAdapter1> adapter;
            for(UINT i=0; factory->EnumAdapters1(i, &adapter)==S_OK; ++i) {
                DXGI_ADAPTER_DESC1 info{}; adapter->GetDesc1(&info);
                if(info.VendorId==4318 && info.DeviceId==10400) break;
                adapter.Reset();
            }
            ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
            HRESULT hr=D3D11CreateDevice(adapter.Get(), adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
                nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context);
            std::printf("d3d=%08lx\n", hr);
            D3D11_TEXTURE2D_DESC desc{}; desc.Width=1080; desc.Height=1920; desc.MipLevels=1; desc.ArraySize=1;
            desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count=1; desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
            desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED_NTHANDLE|D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
            ComPtr<ID3D11Texture2D> texture; hr=device->CreateTexture2D(&desc,nullptr,&texture);
            std::printf("texture=%08lx\n", hr);
            ComPtr<IDXGIResource1> resource; texture.As(&resource);
            HANDLE shared{}; hr=resource->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,nullptr,&shared);
            std::printf("shared=%08lx\n", hr);
            struct Frame { UINT64 id, time; bool nt; unsigned char pad[7]; HANDLE handle; DWORD pid; DWORD reserved; };
            Frame frame{(UINT64(1920)<<32)|1080,GetTickCount64(),true,{},shared,GetCurrentProcessId(),0};
            std::printf("send=%d\n",send(id,1,&frame,sizeof(frame))); std::fflush(stdout);
            ComPtr<IDXGIKeyedMutex> mutex; texture.As(&mutex);
            for(int i=0;i<3;++i) {
                hr=mutex->AcquireSync(1,1000); std::printf("acquire1=%08lx\n",hr);
                if(hr==S_OK) mutex->ReleaseSync(0);
                Sleep(100);
            }
            Sleep(1000);
            CloseHandle(shared);
        }
        Sleep(1000);
    }
    destroy(id);
    return result == 0 ? 0 : 1;
}
