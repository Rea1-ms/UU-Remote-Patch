#pragma once
#include "common.hpp"
#include "input.hpp"

namespace uu {
struct Frame { int width=0,height=0; uint64_t sequence=0; std::vector<uint8_t> pixels; };
class Session {
public:
    using Resize = std::function<void(int,int)>;
    explicit Session(Device device, Resize resize);
    ~Session();
    bool start();
    void stop();
    std::shared_ptr<const Frame> latest();
    bool event(std::string_view command) { return input_.event(command); }
    bool text(std::string_view text) { return input_.text(text); }
    bool alive() const { return !stopped_ && ready_; }
private:
    Device device_;
    Resize resize_;
    std::thread reader_, drainer_;
    std::mutex frame_mutex_, send_mutex_;
    std::condition_variable frame_ready_;
    std::shared_ptr<const Frame> frame_;
    std::atomic<bool> stopped_{false}, ready_{false};
    std::atomic<SOCKET> video_{INVALID_SOCKET}, control_{INVALID_SOCKET};
    HANDLE server_ = nullptr;
    int port_ = 0;
    Input input_;
    bool send(const std::vector<uint8_t>& bytes);
    void read_video();
};
}
