#include "ring_buffer.h"
#include <atomic>
#include <cstring>
#include <stdexcept>

namespace netroom::ipc {

namespace {
constexpr std::uint32_t kSlotOverhead = sizeof(ImeFrameHeader);
constexpr std::uint32_t kSlotContent  = kSlotBytes - sizeof(ImeFrameHeader);
}

RingBuffer::RingBuffer(void* region, std::size_t regionBytes, Role role)
    : regionBytes_(regionBytes), role_(role) {
    if (region == nullptr || regionBytes < RequiredRegionBytes())
        throw std::invalid_argument("RingBuffer region too small or null");
    hdr_ = reinterpret_cast<ImeSharedRingHeader*>(region);
    slots_ = reinterpret_cast<std::uint8_t*>(reinterpret_cast<std::uint8_t*>(region) + HeaderBytes());

    if (role_ == Role::Producer) {
        std::memset(hdr_, 0, HeaderBytes());          // 清零头部后按契约重建
        hdr_->magic      = kRingMagic;
        hdr_->abiVersion = kAbiVersion;
        hdr_->slotCount  = kMaxSlots;
        hdr_->slotBytes  = kSlotBytes;
        hdr_->state      = static_cast<std::uint32_t>(RingState::Ready);
        hdr_->writeSeq   = 0;
        hdr_->consumed   = 0;
    }
}

void* RingBuffer::slotPtr(std::uint64_t seq) const {
    return slots_ + static_cast<std::size_t>(seq % hdr_->slotCount) * kSlotBytes;
}

bool RingBuffer::validateHeader(const ImeFrameHeader& h) const {
    static_assert(kSlotContent >= sizeof(ImeFrameHeader), "slot too small");
    return h.magic == kFrameMagic &&
           h.length <= kSlotContent &&
           h.kind >= static_cast<std::uint16_t>(FrameKind::Key) &&
           h.kind <= static_cast<std::uint16_t>(FrameKind::Feedback);
}

bool RingBuffer::Produce(const ImeFrameHeader& hdr, const void* body, std::uint32_t bodyLen,
                         std::uint64_t tsNow) {
    if (role_ != Role::Producer) return false;
    if (!validateHeader(hdr)) return false;
    if (bodyLen > kSlotContent) return false;
    // 满则拒绝，绝不覆盖未消费数据
    if (hdr_->writeSeq - hdr_->consumed >= hdr_->slotCount) return false;

    std::uint64_t seq = hdr_->writeSeq;
    ImeFrameHeader* dst = reinterpret_cast<ImeFrameHeader*>(slotPtr(seq));

    dst->magic    = kFrameMagic;
    dst->length   = bodyLen;
    dst->kind     = hdr.kind;
    dst->flags    = hdr.flags;
    dst->frameSeq = static_cast<std::uint32_t>(seq & 0xFFFFFFFFu);
    dst->ts       = tsNow;

    if (bodyLen) std::memcpy(reinterpret_cast<std::uint8_t*>(dst) + sizeof(ImeFrameHeader),
                             body, bodyLen);
    // release 屏障：sotre 数据在可见地推进 writeSeq 之前生效
    std::atomic_thread_fence(std::memory_order_release);
    hdr_->writeSeq = seq + 1;
    return true;
}

bool RingBuffer::Consume(ImeFrameHeader* outHdr, void* outBody, std::size_t capBytes,
                         std::size_t* bodyLenOut, std::uint64_t now) {
    if (role_ != Role::Consumer) return false;
    if (hdr_->writeSeq == hdr_->consumed) return false;

    // acquire 屏障：确保读取数据在写方推进 writeSeq 后可见
    std::atomic_thread_fence(std::memory_order_acquire);
    std::uint64_t seq = hdr_->consumed;
    const std::uint8_t* p = reinterpret_cast<const std::uint8_t*>(slotPtr(seq));

    ImeFrameHeader h;
    std::memcpy(&h, p, sizeof(ImeFrameHeader));
    if (!validateHeader(h)) {
        // 损坏帧：丢弃该槽并推进，防止死循环
        hdr_->consumed = seq + 1;
        return false;
    }

    if (outHdr) *outHdr = h;
    if (bodyLenOut) *bodyLenOut = h.length;
    if (h.length && outBody) {
        if (capBytes < h.length) return false;      // 调用方缓冲不足，本轮不消费
        std::memcpy(outBody, p + sizeof(ImeFrameHeader), h.length);
    }
    hdr_->consumed = seq + 1;
    (void)now;
    return true;
}

bool RingBuffer::IsEmpty() const {
    if (role_ != Role::Consumer) return false;
    return hdr_->writeSeq == hdr_->consumed;
}

}  // namespace netroom::ipc
