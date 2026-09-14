#include "netroom/daemon/pinyin_engine.h"
#include "netroom/ipc/ime_channel.h"
#include "netroom/ipc/frame_codec.h"
#include <cstdint>
#include <iostream>
#include <vector>

using namespace netroom::ipc;
using namespace netroom::engine;

static int failures=0;
#define CHECK(c) do{ if(!(c)){++failures; std::cerr<<"FAIL L"<<__LINE__<<": " #c "\n";} }while(0)

static ImeKeyBody keyDown(std::uint32_t vk){ ImeKeyBody k{}; k.vk=vk; k.flags=1; return k; }

int main(){
    PinyinEngine eng;
    CandidateFrame out;

    // 1) 键入 n -> 组合态, composition="n"
    CHECK(eng.ProcessKey(keyDown('N'), &out));
    CHECK(out.status==0 && out.composition=="n");

    // 2) 键入 i -> "ni"，候选含"你"
    CHECK(eng.ProcessKey(keyDown('I'), &out));
    CHECK(out.status==0 && out.composition=="ni");
    bool hasNi=false; for(auto&it:out.items) if(it.text=="你") hasNi=true;
    CHECK(hasNi);

    // 3) 空格提交首选
    CHECK(eng.ProcessKey(keyDown(0x20), &out));
    CHECK(out.status==1);
    CHECK(eng.input().empty());

    // 4) 非拼音起始字母放行
    CHECK(!eng.ProcessKey(keyDown('X'), &out));

    // 5) 完整音节 + 数字候选
    eng.Reset();
    CHECK(eng.ProcessKey(keyDown('Z'), &out));
    CHECK(eng.ProcessKey(keyDown('H'), &out));
    CHECK(eng.ProcessKey(keyDown('U'), &out));   // zhu
    CHECK(out.status==0 && out.composition=="zhu");
    CHECK(eng.ProcessKey(keyDown('2'), &out));   // 第2候选"住"
    CHECK(out.status==1);

    // 6) 退格删除
    eng.Reset();
    CHECK(eng.ProcessKey(keyDown('H'), &out));
    CHECK(eng.ProcessKey(keyDown('A'), &out));
    CHECK(eng.ProcessKey(keyDown(0x08), &out));  // backspace -> "h"
    CHECK(out.status==0 && out.composition=="h");
    CHECK(eng.ProcessKey(keyDown(0x08), &out));  // backspace -> empty
    CHECK(out.status==2);

    // 7) ESC 放弃
    eng.Reset();
    CHECK(eng.ProcessKey(keyDown('W'), &out));
    CHECK(eng.ProcessKey(keyDown(0x1B), &out));
    CHECK(out.status==2 && eng.input().empty());

    if (failures){ std::cerr<<failures<<" FAILURES\n"; return 1; }
    std::cout<<"pinyin engine tests OK\n"; return 0;
}