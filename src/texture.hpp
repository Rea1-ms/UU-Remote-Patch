#pragma once
#include "session.hpp"
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
namespace uu {
struct SharedFrame {
    uint32_t width, height;
    int64_t timestamp;
    bool nt_handle;
    uint8_t padding[7];
    HANDLE handle;
    uint32_t pid, reserved;
};
static_assert(sizeof(SharedFrame)==40);
class TextureWriter {
public:
    TextureWriter();
    std::string info(int width,int height) const;
    bool write(const SharedFrame& destination,const Frame& frame);
private:
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11Device> device_;
    Ptr<ID3D11DeviceContext> context_;
    UINT vendor_=0,device_id_=0;
    std::mutex mutex_;
};
}
