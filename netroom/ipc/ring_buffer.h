#pragma once
#include "ime_shared.h"
#include <cstddef>
#include <cstdint>

namespace netroom::ipc {

// SPSC 环形缓冲区（单生产者 / 单消费者），无锁，作用于调用方提供的共享内存区域。
// 槽位闭环：stored-index = (producer|consumed seq) % slotCount。
// - 空判据：writeSeq == consumed
// - 满判据：writeSeq - consumed >= slotCount
//   （满时 Producer 拒绝并返回 false，绝不覆盖未消费数据；调用方决定丢弃/降级策略）
class RingBuffer {
public:
    enum class Role { Producer, Consumer };

    RingBuffer(void* region, std::size_t regionBytes, Role role,
                 bool initHeader = true);
    ~RingBuffer() = default;

    static constexpr std::size_t HeaderBytes() {
        return align_up(sizeof(ImeSharedRingHeader), 64u);
    }
    static constexpr std::size_t RequiredRegionBytes() {
        return HeaderBytes() + static_cast<std::size_t>(kMaxSlots) * kSlotBytes;
    }

    // Producer-only。满时返回 false。bodyLen==0 且 body==null 表示无负载帧。
    bool Produce(const ImeFrameHeader& hdr, const void* body, std::uint32_t bodyLen,
                 std::uint64_t tsNow);
    // Consumer-only。空返回 false。capBytes 不足时返回 false（不消费）。
    bool Consume(ImeFrameHeader* outHdr, void* outBody, std::size_t capBytes,
                 std::size_t* bodyLenOut, std::uint64_t now);

    bool IsEmpty() const;

    static constexpr std::size_t align_up(std::size_t n, std::size_t a){ return (n+a-1)/a*a; }

private:
    void* slotPtr(std::uint64_t seq) const;
    bool validateHeader(const ImeFrameHeader& h) const;

    ImeSharedRingHeader* hdr_ = nullptr;
    std::uint8_t* slots_ = nullptr;
    std::size_t regionBytes_ = 0;
    Role role_ = Role::Consumer;
};

}  // namespace netroom::ipc
