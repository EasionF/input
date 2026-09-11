#pragma once
#include "ime_shared.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace netroom::ipc {

// ---- 帧负载编解码契约（纯内存，无 Windows 依赖，两进程共用）----
// Encoder 断言溢出/非法即返回 false；Decoder 对不可信字节流做全长校验 + 边界检查。
// 编码结果是自包含字节序列（字符串内嵌 UTF-8），解码产出拥有所有权的结构。

struct KeyFrame {
    ImeKeyBody body{};
};

enum class CommandKind : std::uint16_t {
    SetFocus      = 1,
    SetExemptList = 2,
    Reset         = 3,
    Terminate     = 4,
};

struct CommandFrame {
    CommandKind   kind      = CommandKind::Reset;
    std::uint64_t threadId  = 0;
    std::uint64_t hwnd      = 0;
    std::string   payload;   // 附加负载（UTF-8）
};

struct CandidateItem {
    CandidateKind kind   = CandidateKind::Hanzi;
    std::uint16_t label  = 0;
    std::string   text;   // 候选正文
    std::string   aux;    // 辅助说明（可为空）
};

struct CandidateFrame {
    std::uint32_t     status = 0;    // 0 active 1 committed 2 abandoned 3 passthrough
    std::uint32_t     caret  = 0;
    std::string       composition;   // 组合串
    std::vector<CandidateItem> items;
};

// cap 为输出缓冲区上限（<=kMaxBodyLen）。成功返回 true。
bool EncodeKey(const KeyFrame& f, std::vector<std::uint8_t>& out, std::uint32_t cap);
bool DecodeKey (const std::uint8_t* buf, std::size_t len, KeyFrame& out);

bool EncodeCommand(const CommandFrame& f, std::vector<std::uint8_t>& out, std::uint32_t cap);
bool DecodeCommand (const std::uint8_t* buf, std::size_t len, CommandFrame& out);

bool EncodeCandidate(const CandidateFrame& f, std::vector<std::uint8_t>& out, std::uint32_t cap);
bool DecodeCandidate (const std::uint8_t* buf, std::size_t len, CandidateFrame& out);

}  // namespace netroom::ipc