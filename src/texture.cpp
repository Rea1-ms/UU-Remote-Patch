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
    std::ostringstream out;out<<w<<' '<<h<<' '<<vendor_<<' '<<device_id_<<" 1 0 "<<config().fps<<' '<<GetCurrentProcessId();return out.str();
}
bool TextureWriter::write(const SharedFrame& shared,const Frame& frame) {
    if(shared.width<1 || shared.height<1 || shared.width>4096 || shared.height>4096 || !shared.handle)return false;
    // These handles belong to the host process. Never close the caller's handle.
    if(shared.pid!=GetCurrentProcessId())return false;
    std::lock_guard lock(mutex_);
    auto key=std::make_pair(shared.handle,shared.nt_handle);
    auto found=targets_.find(key);
    if(found==targets_.end()) {
        Target target;
        HRESULT hr;
        if(shared.nt_handle) {
            Ptr<ID3D11Device1> d;device_.As(&d);if(!d)return false;
            hr=d->OpenSharedResource1(shared.handle,IID_PPV_ARGS(&target.texture));
        } else hr=device_->OpenSharedResource(shared.handle,IID_PPV_ARGS(&target.texture));
        if(FAILED(hr) || FAILED(target.texture.As(&target.mutex))) {log("shared texture open failed");return false;}
        if(targets_.size()>=4)targets_.clear();
        found=targets_.emplace(key,std::move(target)).first;
    }
    auto& target=found->second;
    D3D11_TEXTURE2D_DESC desc{};target.texture->GetDesc(&desc);
    if(desc.Width!=shared.width || desc.Height!=shared.height || desc.SampleDesc.Count!=1 ||
       (desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM && desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM))return false;
    if(target.mutex->AcquireSync(0,100)!=S_OK)return false;
    const uint8_t* pixels=frame.pixels.data();
    std::vector<uint8_t> scaled;
    if(shared.width!=uint32_t(frame.width) || shared.height!=uint32_t(frame.height) || desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM) {
        scaled.resize(size_t(shared.width)*shared.height*4);
        for(UINT y=0;y<shared.height;++y)for(UINT x=0;x<shared.width;++x) {
            auto* dst=scaled.data()+(size_t(y)*shared.width+x)*4;
            const auto* src=pixels+(size_t(uint64_t(y)*frame.height/shared.height)*frame.width+size_t(uint64_t(x)*frame.width/shared.width))*4;
            std::copy_n(src,4,dst);
            if(desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM)std::swap(dst[0],dst[2]);
        }
        pixels=scaled.data();
    }
    context_->UpdateSubresource(target.texture.Get(),0,nullptr,pixels,shared.width*4,0);
    context_->Flush();
    return SUCCEEDED(target.mutex->ReleaseSync(1));
}
}
