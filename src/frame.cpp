#include "frame.hpp"
#include <algorithm>
namespace uu {
std::pair<int,int> Geometry::display_point(int raw_x,int raw_y) const {
    if(width<1 || height<1)return {0,0};
    raw_x=std::clamp(raw_x,0,raw_width()-1);
    raw_y=std::clamp(raw_y,0,raw_height()-1);
    return landscape()?std::pair{raw_y,height-1-raw_x}:std::pair{raw_x,raw_y};
}
std::vector<uint8_t> frame_pixels(const Frame& frame,int width,int height,bool raw,bool rgba) {
    if(width<1 || height<1 || width>4096 || height>4096 || frame.width<1 || frame.height<1 ||
       frame.width>4096 || frame.height>4096 || frame.pixels.size()!=size_t(frame.width)*frame.height*4)
        throw std::runtime_error("invalid frame dimensions");
    auto geometry=frame.geometry();
    int source_w=raw?geometry.raw_width():frame.width,source_h=raw?geometry.raw_height():frame.height;
    std::vector<uint8_t> output(size_t(width)*height*4);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
        int sx=int(int64_t(x)*source_w/width),sy=int(int64_t(y)*source_h/height);
        auto point=raw?geometry.display_point(sx,sy):std::pair{sx,sy};
        auto* dst=output.data()+(size_t(y)*width+x)*4;
        auto* src=frame.pixels.data()+(size_t(point.second)*frame.width+point.first)*4;
        std::copy_n(src,4,dst);if(rgba)std::swap(dst[0],dst[2]);
    }
    return output;
}
}
