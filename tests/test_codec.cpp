#include "netroom/ipc/frame_codec.h"
#include <cstdint>
#include <iostream>
#include <vector>
using namespace netroom::ipc;
static int failures=0;
#define CHECK(c) do{ if(!(c)){++failures; std::cerr<<"FAIL L"<<__LINE__<<": " #c "\n";} }while(0)

int main(){
    KeyFrame k; k.body.vk=0x41; k.body.scan=0x1E; k.body.flags=1; k.body.threadId=7; k.body.hwnd=0x1234; k.body.ts=99;
    std::vector<std::uint8_t> buf; CHECK(EncodeKey(k,buf,256));
    KeyFrame k2; CHECK(DecodeKey(buf.data(),buf.size(),k2));
    CHECK(k2.body.vk==k.body.vk && k2.body.hwnd==k.body.hwnd && k2.body.ts==k.body.ts && k2.body.threadId==k.body.threadId);

    CommandFrame c; c.kind=CommandKind::SetFocus; c.threadId=5; c.hwnd=0xABCD; c.payload="chrome";
    std::vector<std::uint8_t> cb; CHECK(EncodeCommand(c,cb,512));
    CommandFrame c2; CHECK(DecodeCommand(cb.data(),cb.size(),c2));
    CHECK(c2.kind==CommandKind::SetFocus && c2.payload=="chrome");

    CandidateFrame f; f.status=0; f.caret=2; f.composition="zhu";
    f.items.push_back({CandidateKind::Hanzi,1,"猪","pig"});
    f.items.push_back({CandidateKind::English,2,"pig","英"});
    f.items.push_back({CandidateKind::Emoji,3,"🐷",""});
    std::vector<std::uint8_t> vb; CHECK(EncodeCandidate(f,vb,2048));
    CandidateFrame f2; CHECK(DecodeCandidate(vb.data(),vb.size(),f2));
    CHECK(f2.status==0 && f2.caret==2 && f2.composition=="zhu");
    CHECK(f2.items.size()==3);
    CHECK(f2.items[0].kind==CandidateKind::Hanzi && f2.items[0].text=="猪" && f2.items[0].aux=="pig");
    CHECK(f2.items[1].text=="pig" && f2.items[1].aux=="英");
    CHECK(f2.items[2].kind==CandidateKind::Emoji && f2.items[2].text=="🐷" && f2.items[2].aux=="");

    CandidateFrame bad;
    // 翻转 itemCount 字段(offset 8, little-endian)为 0xFFFF，导致读取越界被拒
    std::vector<std::uint8_t> corrupt(vb.begin(), vb.end());
    if(corrupt.size()>=10){ corrupt[8]=0xFF; corrupt[9]=0xFF; }
    CHECK(!DecodeCandidate(corrupt.data(),corrupt.size(),bad));
    // 截断拒绝
    CHECK(!DecodeCandidate(vb.data(), vb.size()-1, bad));
    // 空输入拒绝
    CHECK(!DecodeCandidate(nullptr,0,bad));

    if(failures){ std::cerr<<failures<<" FAILURES\n"; return 1; }
    std::cout<<"codec tests OK\n"; return 0;
}