#pragma once
#include "../ipc/frame_codec.h"
#include <cstdint>
#include <string>

namespace netroom::host {

// 宿主侧组合语义：把守护进程产出的候选帧翻译成可作用于目标窗口的动作。
// 只做规约与计算，不含任何 TSF/COM 机制 —— 因此可无头单测。
// 说明：候选帧 status：0=组合 1=已提交 2=放弃 3=放行。
enum class HostActionKind : std::uint8_t {
    Passthrough = 0,       // 不处理，原样放行按键
    UpdateComposition = 1, // 更新组合串文本（utf8 + caret）
    Commit = 2,            // 上屏给定文本
    ClearComposition = 3,  // 清空当前组合
};

struct HostAction {
    HostActionKind kind = HostActionKind::Passthrough;
    std::string utf8Text;     // Commit 的提交文本 / UpdateComposition 的组合串
    std::uint32_t caret = 0;  // UpdateComposition 时光标码元位置
};

// 纯函数：帧 -> 动作。
HostAction Apply(const netroom::ipc::CandidateFrame& frame);

// UTF-8 -> UTF-16（TSF/Windows 侧文本一律 UTF-16）。失败返回 false。
bool Utf8ToWide(const std::string& utf8, std::wstring* wide);

}  // namespace netroom::host