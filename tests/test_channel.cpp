#include "netroom/ipc/ime_channel.h"
#include "netroom/ipc/ring_buffer.h"
#include <windows.h>
#include <cstdint>
#include <iostream>

using namespace netroom::ipc;

static int failures = 0;
#define CHECK(cond) do { if(!(cond)){ ++failures; std::cerr << "FAIL L" << __LINE__ << ": " #cond "\n"; } } while(0)

static ImeFrameHeader mk(FrameKind k){ ImeFrameHeader h{}; h.magic=kFrameMagic; h.kind=static_cast<uint16_t>(k); return h; }

int main(){
    // 模拟宿主进程(Host) <-> 守护进程(Daemon) 单帧往返。
    const std::wstring chName = L"tst_ch_2";
    const std::wstring evName = L"tst_ev_2";

    // Daemon 侧：作为候选生产者（creator）。Host 侧作为消费者（connector）。
    ImeChannel daemon(chName, evName, RingBuffer::Role::Producer, true);

    const int N = 50;
    for (int i=0;i<N;++i){
        // 生产者线程/进程写一帧 -> 通知
        ImeKeyBody b{}; b.vk=(uint32_t(i)); b.ts=(uint64_t(i))*2;
        bool ok = daemon.Ring().Produce(mk(FrameKind::Key), &b, sizeof(b), 0);
        CHECK(ok);
        daemon.Notify();

        // 消费者（同进程，模拟对端；跨进程用对象名相同即可）
        ImeChannel host(chName, evName, RingBuffer::Role::Consumer, false);
        for(int tries=0;tries<1000;++tries){
            DWORD w = host.WaitForData(200);
            if (w != WAIT_OBJECT_0) continue;
            ImeFrameHeader outH; std::uint8_t out[64]; std::size_t len=0;
            if (host.Ring().Consume(&outH, out, sizeof(out), &len, 0)){
                ImeKeyBody* rb=reinterpret_cast<ImeKeyBody*>(out);
                CHECK(rb->vk==(uint32_t)i && rb->ts==(uint64_t)i*2);
                break;
            }
        }
        // 客户端通道销毁后再进入下一轮（模拟独立连接）
    }

    if (failures){ std::cerr<<failures<<" FAILURES\n"; return 1; }
    std::cout<<"channel test OK\n";
    return 0;
}