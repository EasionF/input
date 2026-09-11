#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <msctf.h>
#include <cstdint>
#include <memory>
#include "tfs_guids.h"
#include "../ipc/ime_channel.h"
#include "../ipc/frame_codec.h"

// netroom TSF 文本服务
// 职责（净室前端壳）：按键拦截 -> 转发 Daemon -> 据此驱动 Composition / Commit。
// 不持有候选语义，不渲染任何 UI（UI 归 Daemon）。
class CTextService : public ITfTextInputProcessorEx,
                     public ITfThreadMgrEventSink,
                     public ITfKeyEventSink,
                     public ITfCompositionSink,
                     public ITfCleanupContextSink {
public:
    CTextService();
    ~CTextService();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfTextInputProcessor
    STDMETHODIMP Activate(ITfThreadMgr* ptim, TfClientId tid) override;
    STDMETHODIMP Deactivate() override;
    // ITfTextInputProcessorEx
    STDMETHODIMP ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD dwFlags) override;

    // ITfThreadMgrEventSink
    STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr* pdim) override;
    STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr* pdim) override;
    STDMETHODIMP OnSetFocus(ITfDocumentMgr* pdimFocus, ITfDocumentMgr* pdimPrevFocus) override;
    STDMETHODIMP OnPushContext(ITfContext* pContext) override;
    STDMETHODIMP OnPopContext(ITfContext* pContext) override;

    // ITfKeyEventSink
    STDMETHODIMP OnSetFocus(BOOL fForeground) override;
    STDMETHODIMP OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnTestKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnPreservedKey(ITfContext* pic, REFGUID rguid, BOOL* pfEaten) override;

    // ITfCompositionSink
    STDMETHODIMP OnCompositionTerminated(TfEditCookie ecWrite, ITfComposition* pComposition) override;

    // ITfCleanupContextSink
    STDMETHODIMP OnCleanupContext(TfEditCookie ecRead, ITfContext* pContext) override;

private:
    bool MoveToTLS();
    void InitIpc();               // 连接 Daemon 通道（失败即 passthrough）
    void TeardownIpc();
    bool ForwardKey(bool isDown, bool isRepeat, WPARAM vk, LPARAM lParam,
                    ITfContext* pic);
    bool ProcessDaemonResponse(const std::vector<std::uint8_t>& frame,
                               ITfContext* pic, bool* handled);
    bool StartComposition(ITfContext* pic);
    bool SetCompositionString(ITfContext* pic, const std::string& utf8,
                              std::uint32_t caret);
    bool CommitComposition(ITfContext* pic, const std::string& utf8);
    void ClearCompositionCache();

    // 缓存焦点上下文（非自增持有，生命周期由 TSF 保证在 Activate 存活期内）
    ITfContext* focusContext_ = nullptr;
    ITfDocumentMgr* focusDocMgr_ = nullptr;

    // IPC（仅当一个方向是 Producer）。注意：TextService.dll 是被注入宿主进程，
    // Daemon 是独立进程；此处使用命名共享内存通道。
    std::unique_ptr<netroom::ipc::ImeChannel> keySend_;   // Host(Producer)->Daemon
    std::unique_ptr<netroom::ipc::ImeChannel> candRecv_;  // Daemon(Producer)->Host(Consumer)
    bool ipcReady_ = false;

    // 组合状态
    bool composing_ = false;
    std::string pendingText_;   // 最近候选帧的组合串（UTF-8）用作兜底

    // 引用计数
    LONG refCount_ = 1;
    ITfThreadMgr* threadMgr_ = nullptr;
    TfClientId clientId_ = 0;
    DWORD dwThreadMgrEventSinkCookie_ = 0;
    DWORD dwKeyEventSinkCookie_ = 0;
    DWORD dwCleanupSinkCookie_ = 0;
};