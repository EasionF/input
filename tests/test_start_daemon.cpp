// 端到端集成测试：拉起独立 daemon.exe（独立进程），Host 侧经命名共享内存连通，
// 验证 键击 -> 拼音组合 -> 候选帧 全链路。可无头运行。
#include "netroom/ipc/ime_channel.h"
#include "netroom/ipc/frame_codec.h"
#include <windows.h>
#include <cstdint>
#include <iostream>
#include <string>
#include <cwchar>
#include <vector>

using namespace netroom::ipc;

static int failures=0;
#define CHECK(c) do{ if(!(c)){++failures; std::cerr<<"FAIL L"<<__LINE__<<": " #c "\n";} }while(0)

static ImeKeyBody keyDown(std::uint32_t vk){ ImeKeyBody k{}; k.vk=vk; k.flags=1; return k; }

static bool sendKey(ImeChannel& ch, std::uint32_t vk) {
    ImeFrameHeader h{}; h.magic=kFrameMagic; h.kind=static_cast<std::uint16_t>(FrameKind::Key);
    KeyFrame kf; kf.body = keyDown(vk);
    std::vector<std::uint8_t> enc;
    if (!EncodeKey(kf, enc, kMaxBodyLen)) return false;
    if (!ch.Ring().Produce(h, enc.data(), (std::uint32_t)enc.size(), 0)) return false;
    ch.Notify();
    return true;
}

int main() {
    // 唯一会话名，避免与真实 daemon / 并行测试冲突
    std::wstring base = L"itest_" + std::to_wstring(::GetCurrentProcessId()) + L"_";
    // 用本可执行文件的同目录定位 netroom_daemon.exe，兼容 ctest 工作目录不在 exe 目录的场景
    wchar_t self[MAX_PATH]={0};
    ::GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring dir(self); size_t slash = dir.find_last_of(L"\\");
    if (slash != std::wstring::npos) dir.erase(slash);
    std::wstring daemonPath = dir + L"\\netroom_daemon.exe";
    std::wstring cmd = L"\"" + daemonPath + L"\" " + base;
    STARTUPINFOW si{}; si.cb=sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW    /* 无窗口常驻 */, nullptr, nullptr, &si, &pi)) {
        std::cerr<<"cannot launch daemon\n"; return 1;
    }
    // 等 daemon 建好通道
    ::Sleep(400);

    ImeChannel keySend(base+L"key", base+L"keyevt", RingBuffer::Role::Producer, false);
    ImeChannel candRecv(base+L"cand", base+L"candevt", RingBuffer::Role::Consumer, false);

    // 发送 "ni"
    CHECK(sendKey(keySend, 'N'));
    CHECK(sendKey(keySend, 'I'));

    // 等待候选帧 "ni"
    bool got=false; CandidateFrame cf;
    for (int t=0;t<25 && !got;++t){
        DWORD w=candRecv.WaitForData(200);
        if (w!=WAIT_OBJECT_0) continue;
        while(!candRecv.Ring().IsEmpty()){
            ImeFrameHeader h; std::vector<std::uint8_t> b(kMaxBodyLen); std::size_t len=0;
            if(!candRecv.Ring().Consume(&h,b.data(),b.size(),&len,0)) break;
            if (DecodeCandidate(b.data(), len, cf)) got=true;
        }
    }
    CHECK(got);
    CHECK(cf.status==0 && cf.composition=="ni");

    // 空格提交
    CHECK(sendKey(keySend, 0x20));
    got=false;
    for (int t=0;t<25 && !got;++t){
        DWORD w=candRecv.WaitForData(200);
        if (w!=WAIT_OBJECT_0) continue;
        while(!candRecv.Ring().IsEmpty()){
            ImeFrameHeader h; std::vector<std::uint8_t> b(kMaxBodyLen); std::size_t len=0;
            if(!candRecv.Ring().Consume(&h,b.data(),b.size(),&len,0)) break;
            if (DecodeCandidate(b.data(), len, cf)) got=true;
        }
    }
    CHECK(got && cf.status==1);
    CHECK(cf.composition=="你");

    ::TerminateProcess(pi.hProcess, 0);
    ::CloseHandle(pi.hProcess); ::CloseHandle(pi.hThread);

    if (failures){ std::cerr<<failures<<" FAILURES\n"; return 1; }
    std::cout<<"daemon integration test OK\n"; return 0;
}