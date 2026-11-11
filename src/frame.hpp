#pragma once
#include "common.hpp"
#include <utility>
namespace uu {
struct Geometry {
    int width=0,height=0;
    bool landscape() const {return width>height;}
    int raw_width() const {return landscape()?height:width;}
    int raw_height() const {return landscape()?width:height;}
    int rotation() const {return landscape()?90:0;}
    std::pair<int,int> display_point(int raw_x,int raw_y) const;
};
struct Frame {
    int width=0,height=0;
    uint64_t sequence=0;
    std::vector<uint8_t> pixels;
    Geometry geometry() const {return {width,height};}
};
// Internal frames are upright BGRA. The host texture uses portrait storage;
// its screenshot callback uses upright RGBA instead.
std::vector<uint8_t> frame_pixels(const Frame& frame,int width,int height,bool raw,bool rgba);
}
