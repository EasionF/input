#pragma once
#include <cstdint>
#include <cstddef>

namespace netroom::ipc {

// ---- 稳定性契约：以下所有常量/结构体一旦对外使用即冻结，修改必须升 ABI version ----
inline constexpr std::uint32_t kRingMagic          = 0x494D4521u;  // "IME!"
inline constexpr std::uint32_t kAbiVersion         = 1u;
inline constexpr std::uint32_t kFrameMagic         = 0x4652414Du;  // "FRAM"
inline constexpr std::uint32_t kMaxSlots           = 16u;
inline constexpr std::uint32_t kSlotBytes          = 16384u;       // 单槽负载容量
inline constexpr std::uint32_t kFrameHeaderSizeRaw = 64u;
inline constexpr std::uint32_t kMaxBodyLen         = kSlotBytes - kFrameHeaderSizeRaw;
inline constexpr std::size_t   kHeaderBytes        = 64u;

enum class FrameKind : std::uint16_t {
    Key          = 1,  // Host->Daemon 键击
    Command      = 2,  // Host->Daemon 控制
    Candidate    = 3,  // Daemon->Host 候选帧
    Action       = 4,  // Daemon->Host 提交/终止/替换
    Feedback     = 5,  // 取证/回执（可选）
};

enum class RingState : std::uint32_t {
    Alive = 0,
    Ready = 3,
    Eof   = 1,
    Error = 2,
};

enum class CandidateKind : std::uint16_t {
    Ascii    = 0,
    Hanzi    = 1,
    English  = 2,
    Emoji    = 3,
    Jyutping = 4,
};

struct alignas(64) ImeFrameHeader {
    std::uint32_t magic;     // [0]
    std::uint32_t length;    // payload 字节数（不包含本头）
    std::uint16_t kind;      // FrameKind
    std::uint16_t flags;     // 位标志（保留）
    std::uint32_t frameSeq;  // 帧序号
    std::uint64_t ts;        // 单调时钟
    std::uint32_t pad[10];   // 对齐至 64B [24..63]
};
static_assert(sizeof(ImeFrameHeader) == 64u, "ImeFrameHeader must be 64 bytes");

struct alignas(8) ImeKeyBody {
    std::uint32_t vk;
    std::uint32_t scan;
    std::uint32_t flags;     // bit0 down, bit1 up, bit2 repeat, bit3 extended
    std::uint32_t threadId;
    std::uint64_t hwnd;      // 会话内窗口句柄数值
    std::uint64_t ts;
};
static_assert(sizeof(ImeKeyBody) == 32u, "ImeKeyBody must be 32 bytes");

struct ImeCandidateEntry {
    std::uint16_t kind;      // CandidateKind
    std::uint16_t label;     // 候选序号 1..n
    std::uint16_t compLen;   // 提交时回退组合串字节数
    std::uint16_t textOff;   // UTF-8 文本表偏移
    std::uint16_t textLen;
    std::uint16_t auxOff;    // 辅助说明偏移, 0=无
    std::uint16_t auxLen;
    std::uint16_t pad;
};
static_assert(sizeof(ImeCandidateEntry) == 16u, "ImeCandidateEntry must be 16 bytes");

struct alignas(8) ImeSharedRingHeader {
    std::uint32_t magic;      // [0]
    std::uint32_t abiVersion; // [4]
    std::uint32_t slotCount;  // [8]
    std::uint32_t slotBytes;  // [12]
    std::uint32_t state;      // [16] RingState
    std::uint32_t pad0;       // [20]
    std::uint64_t writeSeq;   // [24] 已写(可读)帧计数
    std::uint64_t consumed;   // [32] 已消费帧计数
    std::uint32_t pad[2];     // [40..47]
};                            // sizeof==48 (align 8 -> 48)
static_assert(sizeof(ImeSharedRingHeader) == 48u, "ring header layout");
static_assert(sizeof(ImeSharedRingHeader) <= 64u, "ring header cap");

}  // namespace netroom::ipc

