#include "netroom/ipc/ring_buffer.h"
#include <cstdint>
#include <iostream>
#include <vector>

using namespace netroom::ipc;

static int failures = 0;
#define CHECK(cond) do { if(!(cond)){ ++failures; std::cerr << "FAIL L" << __LINE__ << ": " #cond "\n"; } } while(0)

static ImeFrameHeader makeHdr(FrameKind k) {
    ImeFrameHeader h{};
    h.magic = kFrameMagic;
    h.kind  = static_cast<std::uint16_t>(k);
    return h;
}

static void test_roundtrip() {
    std::vector<std::uint8_t> r(RingBuffer::RequiredRegionBytes());
    RingBuffer prod(r.data(), r.size(), RingBuffer::Role::Producer);
    RingBuffer cons(r.data(), r.size(), RingBuffer::Role::Consumer);
    CHECK(cons.IsEmpty());

    ImeKeyBody b{}; b.vk=0x5A; b.flags=1; b.ts=777;
    CHECK(prod.Produce(makeHdr(FrameKind::Key), &b, sizeof(b), 999));
    CHECK(!cons.IsEmpty());

    ImeFrameHeader outH; std::uint8_t out[64]; std::size_t len=0;
    CHECK(cons.Consume(&outH, out, sizeof(out), &len, 0));
    CHECK(len == sizeof(b));
    CHECK(outH.kind == static_cast<std::uint16_t>(FrameKind::Key));
    ImeKeyBody* rb = reinterpret_cast<ImeKeyBody*>(out);
    CHECK(rb->vk == 0x5A && rb->ts == 777);
    CHECK(cons.IsEmpty());
}

static void test_wrap_many() {
    std::vector<std::uint8_t> r(RingBuffer::RequiredRegionBytes());
    RingBuffer prod(r.data(), r.size(), RingBuffer::Role::Producer);
    RingBuffer cons(r.data(), r.size(), RingBuffer::Role::Consumer);
    const int N = 200;
    for (int i=0;i<N;++i){
        ImeKeyBody b{}; b.vk=(std::uint32_t)i; b.ts=(std::uint64_t)i*3;
        CHECK(prod.Produce(makeHdr(FrameKind::Key), &b, sizeof(b), 0));
        ImeFrameHeader outH; std::uint8_t out[64]; std::size_t len=0;
        CHECK(cons.Consume(&outH, out, sizeof(out), &len, 0));
        ImeKeyBody* rb = reinterpret_cast<ImeKeyBody*>(out);
        CHECK(rb->vk == (std::uint32_t)i && rb->ts == (std::uint64_t)i*3);
    }
    CHECK(cons.IsEmpty());
}

static void test_full_reject() {
    std::vector<std::uint8_t> r(RingBuffer::RequiredRegionBytes());
    RingBuffer prod(r.data(), r.size(), RingBuffer::Role::Producer);
    RingBuffer cons(r.data(), r.size(), RingBuffer::Role::Consumer);
    ImeKeyBody b{}; bool last=false;
    for (std::uint32_t i=0;i<kMaxSlots;++i){
        b.vk=i;
        last = prod.Produce(makeHdr(FrameKind::Key), &b, sizeof(b), 0);
        CHECK(last);
    }
    b.vk = 999;
    CHECK(!prod.Produce(makeHdr(FrameKind::Key), &b, sizeof(b), 0));
    CHECK(!cons.IsEmpty());
    ImeFrameHeader outH; std::uint8_t out[64]; std::size_t len=0;
    CHECK(cons.Consume(&outH, out, sizeof(out), &len, 0));
    CHECK(prod.Produce(makeHdr(FrameKind::Key), &b, sizeof(b), 0));
}

static void test_reject_oversize() {
    std::vector<std::uint8_t> r(RingBuffer::RequiredRegionBytes());
    RingBuffer prod(r.data(), r.size(), RingBuffer::Role::Producer);
    std::vector<std::uint8_t> big(kMaxBodyLen + 1, 0x11);
    CHECK(!prod.Produce(makeHdr(FrameKind::Candidate), big.data(), static_cast<std::uint32_t>(big.size()), 0));
    std::vector<std::uint8_t> ok(kMaxBodyLen, 0x22);
    CHECK(prod.Produce(makeHdr(FrameKind::Candidate), ok.data(), static_cast<std::uint32_t>(ok.size()), 0));
}

static void test_bad_body_capacity() {
    std::vector<std::uint8_t> r(RingBuffer::RequiredRegionBytes());
    RingBuffer prod(r.data(), r.size(), RingBuffer::Role::Producer);
    RingBuffer cons(r.data(), r.size(), RingBuffer::Role::Consumer);
    ImeKeyBody b{}; b.vk=7;
    CHECK(prod.Produce(makeHdr(FrameKind::Key), &b, sizeof(b), 0));
    ImeFrameHeader outH; std::uint8_t out[4]; std::size_t len=0;
    CHECK(!cons.Consume(&outH, out, sizeof(out), &len, 0)); // cap 不足
    CHECK(!cons.IsEmpty());
    std::uint8_t out2[64];
    CHECK(cons.Consume(&outH, out2, sizeof(out2), &len, 0));
    CHECK(cons.IsEmpty());
}

int main() {
    test_roundtrip();
    test_wrap_many();
    test_full_reject();
    test_reject_oversize();
    test_bad_body_capacity();
    if (failures){ std::cerr << failures << " FAILURES\n"; return 1; }
    std::cout << "ring tests OK\n";
    return 0;
}
