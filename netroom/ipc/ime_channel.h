#pragma once
#include <windows.h>
#include <memory>
#include <string>
#include "ring_buffer.h"

namespace netroom::ipc {

// 单向共享内存通道：镜像 = [ImeSharedRingHeader][slot0..slotN]
// 用于 Deamon <-> 宿主进程间，一端 Producer 另一端 Consumer。
class ImeChannel {
public:
    // 对已建立共享内存区域进行绑定；isCreator 负责初始化 Header。
    // roleForThis 决定本进程在此通道中是 Producer 还是 Consumer。
    ImeChannel(const std::wstring& mapName, const std::wstring& evtName,
               RingBuffer::Role roleForThis, bool isCreator);
    ~ImeChannel();

    ImeChannel(const ImeChannel&) = delete;
    ImeChannel& operator=(const ImeChannel&) = delete;

    RingBuffer& Ring() { return *ring_; }
    const RingBuffer& Ring() const { return *ring_; }

    // Producer 写完一帧后通知对方；Consumer 阻塞等待/轮询
    void Notify();
    DWORD WaitForData(DWORD timeoutMs);

private:
    void* base_ = nullptr;
    std::unique_ptr<RingBuffer> ring_;
    HANDLE mapHandle_ = nullptr;
    HANDLE eventHandle_ = nullptr;
};

}  // namespace netroom::ipc