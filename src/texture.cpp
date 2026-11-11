#include "texture.hpp"
#include <sstream>
#include <algorithm>
namespace uu {
TextureWriter::TextureWriter() {
    Ptr<IDXGIFactory1> factory;
    if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))throw std::runtime_error("DXGI unavailable");
    Ptr<IDXGIAdapter1> adapter;
    for(UINT i=0;factory->EnumAdapters1(i,&adapter)==S_OK;++i) {
        DXGI_ADAPTER_DESC1 desc{};adapter->GetDesc1(&desc);
        if(!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)) {
            auto hr=D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                nullptr,0,D3D11_SDK_VERSION,&device_,nullptr,&context_);
            if(SUCCEEDED(hr)){vendor_=desc.VendorId;device_id_=desc.DeviceId;break;}
        }
        adapter.Reset();
    }
    if(!device_)throw std::runtime_error("compatible graphics adapter unavailable");
}
std::string TextureWriter::info(int w,int h) const {
    Geometry geometry{w,h};
    std::ostringstream out;out<<geometry.raw_width()<<' '<<geometry.raw_height()<<' '<<vendor_<<' '<<device_id_<<" 1 "<<geometry.rotation()<<' '<<config().fps<<' '<<GetCurrentProcessId();return out.str();
}
bool TextureWriter::write(const SharedFrame& shared,const Frame& frame) {
    if(shared.width<1 || shared.height<1 || shared.width>4096 || shared.height>4096 || !shared.handle)return false;
    // These handles belong to the host process. Never close the caller's handle.
    if(shared.pid!=GetCurrentProcessId())return false;
    std::lock_guard lock(mutex_);
    // Reopen for every request: the host can close and reuse a numeric handle
    // during rotation or switching. A cache would retain the retired texture.
    Ptr<ID3D11Texture2D> texture;Ptr<IDXGIKeyedMutex> mutex;
    HRESULT hr;
    if(shared.nt_handle) {
        Ptr<ID3D11Device1> d;device_.As(&d);if(!d)return false;
        hr=d->OpenSharedResource1(shared.handle,IID_PPV_ARGS(&texture));
    } else hr=device_->OpenSharedResource(shared.handle,IID_PPV_ARGS(&texture));
    if(FAILED(hr) || FAILED(texture.As(&mutex))){log("shared texture open failed");return false;}
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
    if(desc.Width!=shared.width || desc.Height!=shared.height || desc.SampleDesc.Count!=1 ||
       (desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM && desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM))return false;
    const uint8_t* pixels=frame.pixels.data();
    std::vector<uint8_t> scaled;
    if(frame.geometry().landscape() || shared.width!=uint32_t(frame.width) || shared.height!=uint32_t(frame.height) || desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM) {
        scaled=frame_pixels(frame,int(shared.width),int(shared.height),true,desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM);
        pixels=scaled.data();
    }
    if(mutex->AcquireSync(0,100)!=S_OK)return false;
    context_->UpdateSubresource(texture.Get(),0,nullptr,pixels,shared.width*4,0);
    context_->Flush();
    return SUCCEEDED(mutex->ReleaseSync(1));
}
}
