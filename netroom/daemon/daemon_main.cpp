// netroom 守护进程：在独立进程内承载输入引擎（librime 或内置拼音），
// 通过共享内存通道与各宿主进程通信。命令行参数可选指定会话名。
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "engine.h"
#include "pinyin_engine.h"
#include "../ipc/ime_channel.h"
#include "../ipc/frame_codec.h"

using namespace netroom::ipc;

static constexpr DWORD kWaitTimeoutMs = 200;

int wmain(int argc, wchar_t** argv) {
    (void)argc;
    const std::wstring base = (argc > 1 && argv[1] && *argv[1] != L'\0')
                                  ? std::wstring(argv[1])
                                  : L"netroom_";

    // 建立两条通道（本进程为 creator，负责初始化共享头）
    ImeChannel keyCh(base + L"key", base + L"keyevt", RingBuffer::Role::Consumer, true);
    ImeChannel candCh(base + L"cand", base + L"candevt", RingBuffer::Role::Producer, true);

    std::unique_ptr<netroom::engine::IEngine> engine =
        std::make_unique<netroom::engine::PinyinEngine>();

    std::wprintf(L"netroom daemon ready on '%ls' (engine: builtin pinyin).\n", base.c_str());
    std::fflush(stdout);

    for (;;) {
        DWORD w = keyCh.WaitForData(kWaitTimeoutMs);
        if (w == WAIT_OBJECT_0) {
            // 消费本批键击
            CandidateFrame out;
            while (!keyCh.Ring().IsEmpty()) {
                ImeFrameHeader hdr;
                std::vector<std::uint8_t> body(static_cast<std::size_t>(kMaxBodyLen));
                std::size_t len = 0;
                if (!keyCh.Ring().Consume(&hdr, body.data(), body.size(), &len, 0))
                    continue;
                if (hdr.kind != static_cast<std::uint16_t>(FrameKind::Key)) {
                    // 类型不符：无非是命令帧或垃圾，安全情况下忽略
                    continue;
                }
                KeyFrame kf;
                if (!DecodeKey(body.data(), len, kf)) continue;

                bool consumed = engine->ProcessKey(kf.body, &out);
                std::wprintf(L"[netroom] recv key frame consumed=%ld\n", consumed ? 1L : 0L);
                std::fflush(stdout);
                if (consumed) {
                    std::vector<std::uint8_t> enc;
                    if (!EncodeCandidate(out, enc, kMaxBodyLen)) continue;
                    ImeFrameHeader oh{};
                    oh.magic = kFrameMagic;
                    oh.kind  = static_cast<std::uint16_t>(FrameKind::Candidate);
                    if (candCh.Ring().Produce(oh, enc.data(), (std::uint32_t)enc.size(), 0)) {
                        candCh.Notify();
                        std::wprintf(L"[netroom] sent candidate frame\n");
                        std::fflush(stdout);
                    }
                }
            }
        }
        // 收到退出信号（本实现用事件形式由外置管理停止；此处为常驻循环）
        if (::GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
            std::wprintf(L"netroom daemon exiting.\n");
            break;
        }
    }
    engine->Reset();
    return 0;
}