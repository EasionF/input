#include "netroom/host/composition_kit.h"
#include <iostream>
using namespace netroom::host;
using namespace netroom::ipc;
static int failures=0;
#define CHECK(c) do{ if(!(c)){++failures; std::cerr<<"FAIL L"<<__LINE__<<": " #c "\n";} }while(0)

int main(){
    CandidateFrame f;
    // 组合
    f.status=0; f.caret=5; f.composition="ni";
    auto a=Apply(f);
    CHECK(a.kind==HostActionKind::UpdateComposition);
    CHECK(a.utf8Text=="ni" && a.caret==5);
    // 提交
    f.status=1; f.composition="你"; f.caret=0;
    a=Apply(f);
    CHECK(a.kind==HostActionKind::Commit && a.utf8Text=="你");
    // 放弃
    f.status=2;
    a=Apply(f);
    CHECK(a.kind==HostActionKind::ClearComposition);
    // 放行
    f.status=3;
    CHECK(Apply(f).kind==HostActionKind::Passthrough);
    // 未知状态保守放行
    f.status=99;
    CHECK(Apply(f).kind==HostActionKind::Passthrough);

    if(failures){ std::cerr<<failures<<" FAILURES\n"; return 1; }
    std::cout<<"composition kit tests OK\n"; return 0;
}