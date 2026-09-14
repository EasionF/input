#pragma once
#include "engine.h"
#include <cstdint>
#include <string>
#include <vector>

namespace netroom::engine {

// 内置拼音引擎：带小规模词库，支持音节级组合 / 候选 / 提交 / 放弃。
// 供守护进程在无 librime 时也能正常组合与判键，同时充当端到端验证样例。
class PinyinEngine : public IEngine {
public:
    PinyinEngine() = default;

    bool ProcessKey(const netroom::ipc::ImeKeyBody& key,
                    netroom::ipc::CandidateFrame* out) override;
    void Reset() override;

    // 单测可读内部状态
    const std::string& input() const { return input_; }

private:
    void BuildActiveCandidate(netroom::ipc::CandidateFrame* out, int status);
    std::string input_;   // 当前累积的拼音字母串（小写）
};

}  // namespace netroom::engine