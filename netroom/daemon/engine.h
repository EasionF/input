#pragma once
#include "../ipc/ime_shared.h"
#include "../ipc/frame_codec.h"
#include <cstdint>

namespace netroom::engine {

// 引擎抽象：守护进程宿主对接的真实输入引擎。
// netroom 默认发行随 librime 绑定；此内置拼音引擎用于无 rime 时可运行的落底实现与端到端验证。
// 任何引擎只需实现 ProcessKey()：给定一次键击，产出候选帧（含状态机 0 组合 /1 提交 /2 放弃 /3 放行）。
class IEngine {
public:
    virtual ~IEngine() = default;
    // 输入：一次键击（vk 为虚拟键码）；输出回填 out。返回 true 表示已消费本次键击。
    virtual bool ProcessKey(const netroom::ipc::ImeKeyBody& key,
                            netroom::ipc::CandidateFrame* out) = 0;
    // 强制状态归零（外部终止/清理时调用）
    virtual void Reset() = 0;
};

}  // namespace netroom::engine