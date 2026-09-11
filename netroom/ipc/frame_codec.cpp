#include "frame_codec.h"
#include <limits>

namespace netroom::ipc {

namespace {
class Writer {
public:
    Writer(std::vector<std::uint8_t>& dst, std::uint32_t cap) : dst_(dst), cap_(cap) { dst_.clear(); }
    bool u16(std::uint16_t v){ if(!room(2)) return false; dst_.push_back(v&0xFF); dst_.push_back((v>>8)&0xFF); return true; }
    bool u32(std::uint32_t v){ if(!room(4)) return false; for(int i=0;i<4;++i) dst_.push_back((v>>(8*i))&0xFF); return true; }
    bool u64(std::uint64_t v){ if(!room(8)) return false; for(int i=0;i<8;++i) dst_.push_back((v>>(8*i))&0xFF); return true; }
    bool str(const std::string& s){
        if(s.size()>std::numeric_limits<std::uint32_t>::max()) return false;
        if(!u32((std::uint32_t)s.size())) return false;
        if(!room(s.size())) return false;
        dst_.insert(dst_.end(), s.begin(), s.end());
        return true;
    }
private:
    bool room(std::size_t n) const { return dst_.size()+n <= cap_ && dst_.size()+n <= std::numeric_limits<std::uint32_t>::max(); }
    std::vector<std::uint8_t>& dst_; std::uint32_t cap_;
};

class Reader {
public:
    Reader(const std::uint8_t* b, std::size_t n): b_(b), n_(n), pos_(0) {}
    bool u16(std::uint16_t& o){ if(!ensure(2)) return false; o=(std::uint16_t)(b_[pos_] | ((std::uint16_t)b_[pos_+1]<<8)); pos_+=2; return true; }
    bool u32(std::uint32_t& o){ if(!ensure(4)) return false; o=0; for(int i=0;i<4;++i) o|=(std::uint32_t)b_[pos_+i]<<(8*i); pos_+=4; return true; }
    bool u64(std::uint64_t& o){ if(!ensure(8)) return false; o=0; for(int i=0;i<8;++i) o|=((std::uint64_t)b_[pos_+i])<<(8*i); pos_+=8; return true; }
    bool str(std::string& o){
        std::uint32_t len; if(!u32(len)) return false;
        if(!ensure(len)) return false;
        o.assign(reinterpret_cast<const char*>(b_+pos_), len); pos_+=len; return true;
    }
    bool eof() const { return pos_ == n_; }
    bool ok()  const { return pos_ <= n_; }
private:
    bool ensure(std::size_t n) const { return n <= n_ && pos_ <= n_ && n <= n_-pos_; }
    const std::uint8_t* b_; std::size_t n_; std::size_t pos_;
};
}  // namespace

bool EncodeKey(const KeyFrame& f, std::vector<std::uint8_t>& out, std::uint32_t cap){
    Writer w(out, cap);
    return w.u32(f.body.vk) && w.u32(f.body.scan) && w.u32(f.body.flags) &&
           w.u32(f.body.threadId) && w.u64(f.body.hwnd) && w.u64(f.body.ts);
}
bool DecodeKey(const std::uint8_t* buf, std::size_t len, KeyFrame& out){
    if(!buf||len!=32) return false;
    Reader r(buf,len);
    return r.u32(out.body.vk) && r.u32(out.body.scan) && r.u32(out.body.flags) &&
           r.u32(out.body.threadId) && r.u64(out.body.hwnd) && r.u64(out.body.ts) && r.eof();
}

bool EncodeCommand(const CommandFrame& f, std::vector<std::uint8_t>& out, std::uint32_t cap){
    Writer w(out, cap);
    return w.u16(static_cast<std::uint16_t>(f.kind)) && w.u64(f.threadId) && w.u64(f.hwnd) && w.str(f.payload);
}
bool DecodeCommand(const std::uint8_t* buf, std::size_t len, CommandFrame& out){
    if(!buf) return false;
    Reader r(buf,len);
    std::uint16_t k;
    if(!r.u16(k)||!r.u64(out.threadId)||!r.u64(out.hwnd)||!r.str(out.payload)||!r.eof()) return false;
    out.kind=static_cast<CommandKind>(k);
    return true;
}

bool EncodeCandidate(const CandidateFrame& f, std::vector<std::uint8_t>& out, std::uint32_t cap){
    if(f.items.size()>std::numeric_limits<std::uint16_t>::max()) return false;
    Writer w(out, cap);
    if(!w.u32(f.status)||!w.u32(f.caret)||!w.u16((std::uint16_t)f.items.size())||!w.u16(0)) return false;
    if(!w.str(f.composition)) return false;
    for(const auto& it : f.items){
        if(!w.u16(static_cast<std::uint16_t>(it.kind))||!w.u16(it.label)) return false;
        if(!w.str(it.text)||!w.str(it.aux)) return false;
    }
    return true;
}
bool DecodeCandidate(const std::uint8_t* buf, std::size_t len, CandidateFrame& out){
    if(!buf) return false;
    Reader r(buf,len);
    std::uint16_t count, zero;
    if(!r.u32(out.status)||!r.u32(out.caret)||!r.u16(count)||!r.u16(zero)) return false;
    if(!r.str(out.composition)) return false;
    out.items.clear(); out.items.reserve(count);
    for(std::uint16_t i=0;i<count;++i){
        CandidateItem it;
        std::uint16_t k;
        if(!r.u16(k)||!r.u16(it.label)) return false;
        it.kind=static_cast<CandidateKind>(k);
        if(!r.str(it.text)||!r.str(it.aux)) return false;
        out.items.push_back(std::move(it));
    }
    return r.eof();
}

}  // namespace netroom::ipc