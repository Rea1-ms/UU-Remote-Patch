#include "input.hpp"
#include <charconv>
#include <algorithm>

namespace uu {
static std::vector<std::string_view> split(std::string_view value, char separator) {
    std::vector<std::string_view> out;
    while (!value.empty()) {
        auto at = value.find(separator);
        out.push_back(value.substr(0, at));
        if (at == value.npos) break;
        value.remove_prefix(at + 1);
    }
    return out;
}
static std::optional<int> number(std::string_view value) {
    int n = 0;
    auto [end, error] = std::from_chars(value.data(), value.data()+value.size(), n);
    if (error != std::errc() || end != value.data()+value.size()) return {};
    return n;
}
void Input::size(int w, int h) {
    std::lock_guard lock(mutex_);
    if(width_!=w || height_!=h){for(auto& [slot,p]:points_)touch(1,uint64_t(p.tracking),p.x,p.y);points_.clear();}
    width_=w;height_=h;
}
bool Input::touch(int action, uint64_t id, int x, int y) {
    if (width_ <= 0 || height_ <= 0 || width_>65535 || height_>65535) return false;
    auto point=Geometry{width_,height_}.display_point(x,y);x=point.first;y=point.second;
    std::vector<uint8_t> msg(32);
    msg[0]=2; msg[1]=uint8_t(action);
    put64(msg.data()+2,id);
    put32(msg.data()+10, uint32_t(std::clamp(x,0,width_-1)));
    put32(msg.data()+14, uint32_t(std::clamp(y,0,height_-1)));
    put16(msg.data()+18,uint16_t(width_)); put16(msg.data()+20,uint16_t(height_));
    put16(msg.data()+22,action==1?0:65535);
    return sink_(msg);
}
bool Input::event(std::string_view commands) {
    if (commands.size()>16384) return false;
    std::lock_guard lock(mutex_);
    for (auto line : split(commands,'\n')) {
        if (!line.empty() && line.back()=='\r') line.remove_suffix(1);
        auto tokens = split(line,':');
        if(tokens.empty()) continue;
        size_t start = number(tokens[0]).has_value() ? 1 : 0;
        if(tokens.size()<=start) return false;
        auto command=tokens[start++];
        if (command=="SLOTMULTIPRESS") {
            if ((tokens.size()-start)%4 || tokens.size()==start) return false;
            // Validate the complete packet before emitting any input.
            for(size_t i=start;i<tokens.size();i+=4) {
                for(size_t j=i;j<i+4;++j) if(!number(tokens[j])) return false;
                if(*number(tokens[i])<0 || *number(tokens[i])>63 || *number(tokens[i+1])<0) return false;
            }
            for(size_t i=start;i<tokens.size();i+=4) {
                int slot=*number(tokens[i]), tracking=*number(tokens[i+1]);
                int x=*number(tokens[i+2]), y=*number(tokens[i+3]);
                auto it=points_.find(slot);
                if(it!=points_.end() && it->second.tracking!=tracking) {
                    if(!touch(1,uint64_t(it->second.tracking),it->second.x,it->second.y)) return false;
                    points_.erase(it); it=points_.end();
                }
                if(!touch(it==points_.end()?0:2,uint64_t(tracking),x,y)) return false;
                points_[slot]={tracking,x,y};
            }
        } else if(command=="SLOTMULTIRELEASE") {
            for(size_t i=start;i<tokens.size();++i) if(!number(tokens[i])) return false;
            for(size_t i=start;i<tokens.size();++i) {
                auto it=points_.find(*number(tokens[i]));
                if(it!=points_.end()) {
                    if(!touch(1,uint64_t(it->second.tracking),it->second.x,it->second.y)) return false;
                    points_.erase(it);
                }
            }
        } else if(command=="KBDPR" || command=="KBDRL") {
            if(tokens.size()!=start+2 || !number(tokens[start]) || !number(tokens[start+1]))return false;
            // MuMu uses Linux input key numbers for these navigation buttons.
            static const std::map<int,int> keys={{158,4},{172,3},{580,187},{102,3},{139,187},{1,111},{14,67},{15,61},{28,66},
                {103,19},{108,20},{105,21},{106,22},{57,62},{114,25},{115,24},{116,26}};
            auto it=keys.find(*number(tokens[start]));if(it==keys.end())return false;
            std::vector<uint8_t> msg(14);msg[0]=0;msg[1]=command=="KBDPR"?0:1;put32(msg.data()+2,uint32_t(it->second));
            if(!sink_(msg))return false;
        } else return false;
    }
    return true;
}
bool Input::system(std::string_view operation) {
    // The official toolbar sends its Home button as tab_manage, not KBDPR.
    return operation=="tab_manage" && event("0:KBDPR:172:1\n0:KBDRL:172:0\n");
}
bool Input::text(std::string_view text) {
    // The server's INJECT_TEXT message accepts at most 300 UTF-8 bytes.
    if (text.empty() || text.size()>300) return false;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0)) return false;
    std::lock_guard lock(mutex_);
    std::vector<uint8_t> msg(5+text.size());
    msg[0]=1; put32(msg.data()+1,uint32_t(text.size()));
    std::copy(text.begin(),text.end(),msg.begin()+5);
    return sink_(msg);
}
void Input::reset() {
    std::lock_guard lock(mutex_);
    for(auto& [slot,p]:points_) touch(1,uint64_t(p.tracking),p.x,p.y);
    points_.clear();
}
}
