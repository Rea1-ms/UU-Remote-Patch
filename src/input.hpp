#pragma once
#include "common.hpp"
#include "frame.hpp"
#include <map>

namespace uu {
class Input {
public:
    using Sink = std::function<bool(const std::vector<uint8_t>&)>;
    explicit Input(Sink sink) : sink_(std::move(sink)) {}
    void size(int width, int height);
    bool event(std::string_view commands);
    bool text(std::string_view value);
    bool system(std::string_view operation);
    void reset();
private:
    struct Point { int tracking, x, y; };
    std::map<int, Point> points_;
    std::mutex mutex_;
    Sink sink_;
    int width_ = 0, height_ = 0;
    bool touch(int action, uint64_t id, int x, int y);
};
}
