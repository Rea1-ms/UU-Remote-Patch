#include "session.hpp"
#include <algorithm>
#include <cstdio>
#include <array>
extern "C" {
#include <libavutil/frame.h>
#include <libavcodec/packet.h>
}

namespace uu {
struct AVCodec;
struct AVCodecContext;
struct Decoder {
    HMODULE util{}, codec{};
    AVCodecContext* context{};
    AVFrame* frame{};
    AVPacket* packet{};
    const AVCodec* (*find)(int){};
    AVCodecContext* (*alloc)(const AVCodec*){};
    int (*open)(AVCodecContext*,const AVCodec*,void*){};
    int (*send)(AVCodecContext*,const AVPacket*){};
    int (*receive)(AVCodecContext*,AVFrame*){};
    void (*free_context)(AVCodecContext**){};
    AVFrame* (*alloc_frame)(){};
    void (*free_frame)(AVFrame**){};
    void (*unref_frame)(AVFrame*){};
    AVPacket* (*alloc_packet)(){};
    void (*free_packet)(AVPacket**){};
    void (*unref_packet)(AVPacket*){};
    int (*new_packet)(AVPacket*,int){};
    template<class T> void sym(T& f, HMODULE lib, const char* name) {
        f=reinterpret_cast<T>(GetProcAddress(lib,name));
        if(!f) throw std::runtime_error("decoder export unavailable");
    }
    Decoder() {
        auto load=[](const wchar_t* name) {
            return LoadLibraryExW((config().scrcpy/name).c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        };
        util=load(L"avutil-61.dll"); codec=load(L"avcodec-63.dll");
        if(!util || !codec) throw std::runtime_error("FFmpeg 63/61 unavailable");
        sym(find,codec,"avcodec_find_decoder");sym(alloc,codec,"avcodec_alloc_context3");
        sym(open,codec,"avcodec_open2");sym(send,codec,"avcodec_send_packet");
        sym(receive,codec,"avcodec_receive_frame");sym(free_context,codec,"avcodec_free_context");
        sym(alloc_frame,util,"av_frame_alloc");sym(free_frame,util,"av_frame_free");sym(unref_frame,util,"av_frame_unref");
        sym(alloc_packet,codec,"av_packet_alloc");sym(free_packet,codec,"av_packet_free");
        sym(unref_packet,codec,"av_packet_unref");sym(new_packet,codec,"av_new_packet");
        const auto* c=find(27); // AV_CODEC_ID_H264, stable public identifier.
        context=alloc(c);frame=alloc_frame();packet=alloc_packet();
        if(!c || !context || !frame || !packet || open(context,c,nullptr)<0) throw std::runtime_error("decoder initialization failed");
    }
    ~Decoder() {
        if(packet)free_packet(&packet);
        if(frame)free_frame(&frame);
        if(context)free_context(&context);
        // Keep modules loaded for the lifetime of the host process.
    }
};
static bool receive_exact(SOCKET s, void* data, size_t count) {
    auto* out=static_cast<char*>(data);
    while(count) {int n=recv(s,out,int(std::min<size_t>(count,1<<20)),0);if(n<=0)return false;out+=n;count-=n;}
    return true;
}
static SOCKET connect_local(int port) {
    SOCKET s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if(s==INVALID_SOCKET)return s;
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(uint16_t(port));address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(connect(s,reinterpret_cast<sockaddr*>(&address),sizeof(address))) {closesocket(s);return INVALID_SOCKET;}
    DWORD timeout=10000;setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));
    timeout=500;setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));
    int yes=1;setsockopt(s,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<const char*>(&yes),sizeof(yes));
    return s;
}
static std::shared_ptr<Frame> convert(AVFrame* source, uint64_t sequence) {
    if(source->width<1 || source->height<1 || source->width>4096 || source->height>4096) return {};
    if(source->format!=AV_PIX_FMT_YUV420P && source->format!=AV_PIX_FMT_YUVJ420P && source->format!=AV_PIX_FMT_NV12)
        throw std::runtime_error("unsupported decoded pixel format");
    auto frame=std::make_shared<Frame>();frame->width=source->width;frame->height=source->height;frame->sequence=sequence;
    frame->pixels.resize(size_t(frame->width)*frame->height*4);
    bool full=source->color_range==AVCOL_RANGE_JPEG || source->format==AV_PIX_FMT_YUVJ420P;
    bool hd=source->colorspace==AVCOL_SPC_BT709;
    for(int y=0;y<frame->height;++y) {
        const auto* luma=source->data[0]+ptrdiff_t(y)*source->linesize[0];
        const auto* u=source->data[1]+ptrdiff_t(y/2)*source->linesize[1];
        const auto* v=source->format==AV_PIX_FMT_NV12?nullptr:source->data[2]+ptrdiff_t(y/2)*source->linesize[2];
        auto* dst=frame->pixels.data()+size_t(y)*frame->width*4;
        for(int x=0;x<frame->width;++x) {
            int cb=(v?u[x/2]:u[(x/2)*2])-128, cr=(v?v[x/2]:u[(x/2)*2+1])-128;
            int yy=full?int(luma[x])*256:std::max(0,int(luma[x])-16)*298;
            int r=yy+(full?(hd?403:359):(hd?459:409))*cr;
            int g=yy-(full?(hd?48:88):(hd?55:100))*cb-(full?(hd?120:183):(hd?136:208))*cr;
            int b=yy+(full?(hd?475:454):(hd?541:516))*cb;
            dst[x*4]=uint8_t(std::clamp((b+128)>>8,0,255));
            dst[x*4+1]=uint8_t(std::clamp((g+128)>>8,0,255));
            dst[x*4+2]=uint8_t(std::clamp((r+128)>>8,0,255));dst[x*4+3]=255;
        }
    }
    return frame;
}
Session::Session(Device d,Resize resize):device_(std::move(d)),resize_(std::move(resize)),input_([this](auto& bytes){return send(bytes);}){}
Session::~Session(){stop();}
bool Session::start() {
    static std::once_flag init;
    std::call_once(init,[]{WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))throw std::runtime_error("socket initialization failed");});
    auto serial=wide(device_.serial);
    auto result=run(config().adb,{L"-s",serial,L"push",(config().scrcpy/L"scrcpy-server").wstring(),L"/data/local/tmp/uu-adb-scrcpy-5.0.1.jar"},20000);
    if(result.code) {log("server upload failed for instance "+std::to_string(device_.index));return false;}
    static std::atomic<uint32_t> next_scid{(GetCurrentProcessId()*2654435761u)^GetTickCount()};
    uint32_t scid=next_scid.fetch_add(1)&0x7fffffff;
    wchar_t hex[9]{};swprintf_s(hex,L"%08x",scid);
    result=run(config().adb,{L"-s",serial,L"forward",L"tcp:0",std::wstring(L"localabstract:scrcpy_")+hex});
    if(result.code)return false;
    try{port_=std::stoi(result.output);}catch(...){return false;}
    server_=spawn(config().adb,{L"-s",serial,L"shell",L"CLASSPATH=/data/local/tmp/uu-adb-scrcpy-5.0.1.jar",
        L"app_process",L"/",L"com.genymobile.scrcpy.Server",L"5.0.1",std::wstring(L"scid=")+hex,
        L"tunnel_forward=true",L"audio=false",L"control=true",L"clipboard_autosync=false",L"cleanup=false",L"power_on=false",
        L"send_device_meta=false",L"video_codec=h264",L"max_fps="+std::to_wstring(config().fps),
        L"max_size="+std::to_wstring(config().max_size),L"video_bit_rate="+std::to_wstring(config().bit_rate)});
    if(!server_)return false;
    for(int attempt=0;attempt<60 && !stopped_;++attempt) {
        auto s=connect_local(port_);
        if(s!=INVALID_SOCKET) {
            uint8_t dummy=1;
            if(receive_exact(s,&dummy,1) && dummy==0){video_=s;break;}
            closesocket(s);
        }
        Sleep(100);
    }
    if(video_==INVALID_SOCKET)return false;
    control_=connect_local(port_);if(control_==INVALID_SOCKET)return false;
    // A static Android screen may send no new frames for minutes. Stop closes
    // both sockets to interrupt blocking reads; idle time is not a disconnect.
    DWORD no_timeout=0;
    for(auto s:{video_.load(),control_.load()})setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&no_timeout),sizeof(no_timeout));
    drainer_=std::thread([this]{char buffer[4096];while(!stopped_ && recv(control_,buffer,sizeof(buffer),0)>0){};});
    reader_=std::thread([this]{read_video();});
    std::unique_lock lock(frame_mutex_);
    frame_ready_.wait_for(lock,std::chrono::seconds(12),[this]{return ready_ || stopped_;});
    return ready_ && !stopped_;
}
void Session::stop() {
    input_.reset();
    stopped_=true; ready_=false;
    for(auto* socket:{&video_,&control_}){auto s=socket->exchange(INVALID_SOCKET);if(s!=INVALID_SOCKET){shutdown(s,SD_BOTH);closesocket(s);}}
    frame_ready_.notify_all();
    if(reader_.joinable())reader_.join();if(drainer_.joinable())drainer_.join();
    if(server_){if(WaitForSingleObject(server_,1500)==WAIT_TIMEOUT)TerminateProcess(server_,0);CloseHandle(server_);server_=nullptr;}
    if(port_) {run(config().adb,{L"-s",wide(device_.serial),L"forward",L"--remove",L"tcp:"+std::to_wstring(port_)});port_=0;}
}
std::shared_ptr<const Frame> Session::latest(){std::lock_guard lock(frame_mutex_);return frame_;}
bool Session::send(const std::vector<uint8_t>& bytes) {
    std::lock_guard lock(send_mutex_);
    if(control_==INVALID_SOCKET || stopped_)return false;
    size_t count=0;
    while(count<bytes.size()){int n=::send(control_,reinterpret_cast<const char*>(bytes.data()+count),int(bytes.size()-count),0);if(n<=0)return false;count+=n;}
    return true;
}
void Session::read_video() {
    try {
        Decoder decoder;
        uint8_t codec[4];
        if(!receive_exact(video_,codec,4) || be32(codec)!=0x68323634)throw std::runtime_error("unexpected video codec");
        std::vector<uint8_t> config_packet;
        uint64_t sequence=0;
        int last_w=0,last_h=0;
        while(!stopped_) {
            uint8_t header[12];if(!receive_exact(video_,header,12))break;
            if(header[0]&0x80){config_packet.clear();continue;}
            auto count=be32(header+8);
            if(count<1 || count>16*1024*1024)throw std::runtime_error("invalid video packet size");
            std::vector<uint8_t> packet(count);
            if(!receive_exact(video_,packet.data(),packet.size()))break;
            if(header[0]&0x40){config_packet.insert(config_packet.end(),packet.begin(),packet.end());if(config_packet.size()>1024*1024)throw std::runtime_error("invalid codec config");continue;}
            decoder.unref_packet(decoder.packet);
            if(decoder.new_packet(decoder.packet,int(packet.size()+config_packet.size()))<0)throw std::bad_alloc();
            std::copy(config_packet.begin(),config_packet.end(),decoder.packet->data);
            std::copy(packet.begin(),packet.end(),decoder.packet->data+config_packet.size());config_packet.clear();
            decoder.packet->pts=int64_t(be64(header)&((uint64_t(1)<<61)-1));
            if(decoder.send(decoder.context,decoder.packet)<0)throw std::runtime_error("video decoding failed");
            while(decoder.receive(decoder.context,decoder.frame)==0) {
                auto frame=convert(decoder.frame,++sequence);decoder.unref_frame(decoder.frame);if(!frame)continue;
                bool resized=last_w!=frame->width || last_h!=frame->height;
                last_w=frame->width;last_h=frame->height;
                input_.size(last_w,last_h);
                {std::lock_guard lock(frame_mutex_);frame_=frame;ready_=true;}
                frame_ready_.notify_all();
                if(resized && resize_)resize_(last_w,last_h);
            }
        }
    }catch(const std::exception& e){log(e.what());}
    stopped_=true;ready_=false;frame_ready_.notify_all();
    log("video disconnected for instance "+std::to_string(device_.index));
}
}
