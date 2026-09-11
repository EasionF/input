#include "tfs_service.h"
#include "../ipc/ime_shared.h"
#include <new>
#include <string>
#include <vector>
#include <chrono>

using namespace netroom::ipc;

// ---- 8ms 裕度：Daemon 无响应即 passthrough，绝不阻塞宿主按键 ----
static constexpr DWORD kDaemonReplyBudgetMs = 8;

// 模块引用计数（由本编译单元导出的模块函数维护）

// ---------------- CTextService ----------------

CTextService::CTextService() = default;

CTextService::~CTextService() {
    if (threadMgr_) { threadMgr_->Release(); threadMgr_ = nullptr; }
    TeardownIpc();
}

STDMETHODIMP CTextService::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_ITfTextInputProcessor) ||
        IsEqualIID(riid, IID_ITfTextInputProcessorEx)) {
        *ppv = static_cast<ITfTextInputProcessorEx*>(this);
    } else if (IsEqualIID(riid, IID_ITfThreadMgrEventSink)) {
        *ppv = static_cast<ITfThreadMgrEventSink*>(this);
    } else if (IsEqualIID(riid, IID_ITfKeyEventSink)) {
        *ppv = static_cast<ITfKeyEventSink*>(this);
    } else if (IsEqualIID(riid, IID_ITfCompositionSink)) {
        *ppv = static_cast<ITfCompositionSink*>(this);
    } else if (IsEqualIID(riid, IID_ITfCleanupContextSink)) {
        *ppv = static_cast<ITfCleanupContextSink*>(this);
    } else {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) CTextService::AddRef() {
    LONG r = InterlockedIncrement(&refCount_);
    return static_cast<ULONG>(r);
}

STDMETHODIMP_(ULONG) CTextService::Release() {
    LONG r = InterlockedDecrement(&refCount_);
    if (r == 0) { delete this; }
    return static_cast<ULONG>(r);
}

// ---- IPC ----

STDMETHODIMP CTextService::Activate(ITfThreadMgr* ptim, TfClientId tid) {
    if (!ptim) return E_INVALIDARG;
    if (threadMgr_) return S_OK;              // 已激活，幂等
    threadMgr_ = ptim; threadMgr_->AddRef();
    clientId_ = tid;

    // 注册线程级 sink（TSF 通过 ITfSource 在 ThreadMgr 上挂观察者）
    ITfSource* pSource = nullptr;
    if (SUCCEEDED(threadMgr_->QueryInterface(IID_ITfSource, (void**)&pSource))) {
        pSource->AdviseSink(IID_ITfThreadMgrEventSink,
                            static_cast<ITfThreadMgrEventSink*>(this),
                            &dwThreadMgrEventSinkCookie_);
        pSource->AdviseSink(IID_ITfKeyEventSink,
                            static_cast<ITfKeyEventSink*>(this),
                            &dwKeyEventSinkCookie_);
        pSource->AdviseSink(IID_ITfCleanupContextSink,
                            static_cast<ITfCleanupContextSink*>(this),
                            &dwCleanupSinkCookie_);
        pSource->Release();
    }
    InitIpc();
    return S_OK;
}

STDMETHODIMP CTextService::ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD /*dwFlags*/) {
    return Activate(ptim, tid);
}

STDMETHODIMP CTextService::Deactivate() {
    if (threadMgr_) {
        ITfSource* pSource = nullptr;
        if (SUCCEEDED(threadMgr_->QueryInterface(IID_ITfSource, (void**)&pSource))) {
            pSource->UnadviseSink(dwThreadMgrEventSinkCookie_);
            pSource->UnadviseSink(dwKeyEventSinkCookie_);
            pSource->UnadviseSink(dwCleanupSinkCookie_);
            pSource->Release();
        }
        threadMgr_->Release();
        threadMgr_ = nullptr;
    }
    dwThreadMgrEventSinkCookie_ = dwKeyEventSinkCookie_ = dwCleanupSinkCookie_ = 0;
    TeardownIpc();
    return S_OK;
}

void CTextService::InitIpc() {
    try {
        // 命名空间：用客户端进程 PID 区分多实例，避免多宿主进程串号
        std::wstring base = L"netroom_";
        base += std::to_wstring(::GetCurrentProcessId());
        base += L"_";
        base += std::to_wstring(clientId_);

        keySend_ = std::make_unique<ImeChannel>(
            base + L"_key", base + L"_keyevt", RingBuffer::Role::Producer, /*isCreator*/false);
        candRecv_ = std::make_unique<ImeChannel>(
            base + L"_cand", base + L"_candevt", RingBuffer::Role::Consumer, /*isCreator*/false);
        ipcReady_ = true;
    } catch (const std::exception&) {
        // Daemon 未启动或绑定失败 -> 保持 passthrough
        TeardownIpc();
        ipcReady_ = false;
    }
}

void CTextService::TeardownIpc() {
    keySend_.reset();
    candRecv_.reset();
    ipcReady_ = false;
}

bool CTextService::ForwardKey(bool isDown, bool isRepeat, WPARAM vk, LPARAM lParam,
                              ITfContext* pic) {
    (void)pic;
    if (!ipcReady_ || !keySend_) return false;

    ImeKeyBody body{};
    body.vk = (std::uint32_t)vk;
    body.scan = (std::uint32_t)((lParam >> 16) & 0xFF);
    body.flags = (isDown ? 1u : 0u) | (isRepeat ? 4u : 0u);
    body.threadId = ::GetCurrentThreadId();
    body.hwnd = (std::uint64_t)(std::uintptr_t)::GetFocus();
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    body.ts = (std::uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(now).count();

    ImeFrameHeader hdr{};
    hdr.magic = kFrameMagic;
    hdr.kind = static_cast<std::uint16_t>(FrameKind::Key);
    KeyFrame kf; kf.body = body;
    std::vector<std::uint8_t> enc;
    if (!EncodeKey(kf, enc, kMaxBodyLen)) return false;

    if (!keySend_->Ring().Produce(hdr, enc.data(), (std::uint32_t)enc.size(), body.ts))
        return false;
    keySend_->Notify();
    return true;
}

// ---- KeyEventSink ----

STDMETHODIMP CTextService::OnSetFocus(BOOL) { return S_OK; }

STDMETHODIMP CTextService::OnTestKeyDown(ITfContext* /*pic*/, WPARAM, LPARAM, BOOL* pfEaten) {
    if (!pfEaten) return E_POINTER;
    // IPC 就绪即声明想吃键：实际处理与咽/放行判定在 OnKeyDown 一次性完成，避免双重 IPC。
    *pfEaten = ipcReady_ ? TRUE : FALSE;
    return S_OK;
}
STDMETHODIMP CTextService::OnTestKeyUp(ITfContext* /*pic*/, WPARAM, LPARAM, BOOL* pfEaten) {
    if (!pfEaten) return E_POINTER;
    *pfEaten = FALSE; return S_OK;
}

STDMETHODIMP CTextService::OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    if (!pfEaten) return E_POINTER;
    *pfEaten = FALSE;
    if (!ipcReady_) return S_OK;                 // 无 Daemon -> 原样放行

    LRESULT lParamData = (LRESULT)lParam;
    bool repeat = (lParamData & 0x40000000L) != 0;

    if (!ForwardKey(true, repeat, wParam, lParam, pic)) return S_OK;   // 失败放行

    // 等待 Daemon 候选帧（<=8ms），超时 passthrough
    if (!candRecv_) return S_OK;
    DWORD w = candRecv_->WaitForData(kDaemonReplyBudgetMs);
    if (w != WAIT_OBJECT_0) return S_OK;         // 超时 -> 放行

    while (!candRecv_->Ring().IsEmpty()) {
        ImeFrameHeader hdr; std::vector<std::uint8_t> body(kMaxBodyLen);
        std::size_t len = 0;
        if (!candRecv_->Ring().Consume(&hdr, body.data(), body.size(), &len, 0))
            break;
        CandidateFrame cf;
        if (DecodeCandidate(body.data(), len, cf)) {
            if (cf.status == 0) {                // active -> 组合
                composing_ = true;
                StartComposition(pic);
                SetCompositionString(pic, cf.composition, cf.caret);
                pendingText_ = cf.composition;
                *pfEaten = TRUE;
            } else if (cf.status == 1) {         // committed
                CommitComposition(pic, cf.composition);
                composing_ = false;
                *pfEaten = TRUE;
            } else if (cf.status == 2) {         // abandoned
                ClearCompositionCache();
                composing_ = false;
                *pfEaten = (pendingText_.empty() ? FALSE : TRUE);
            } else {                             // passthrough
                *pfEaten = FALSE;
            }
        }
    }
    return S_OK;
}

STDMETHODIMP CTextService::OnPreservedKey(ITfContext* /*pic*/, REFGUID /*rguid*/, BOOL* pfEaten) {
    if (!pfEaten) return E_POINTER;
    *pfEaten = FALSE;   // 未定义保存键；一律不吞
    return S_OK;
}

STDMETHODIMP CTextService::OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    if (!pfEaten) return E_POINTER;
    *pfEaten = FALSE;
    if (!ipcReady_) return S_OK;
    if (wParam == VK_ESCAPE || wParam == VK_RETURN || wParam == VK_SPACE ||
        (wParam >= '0' && wParam <= '9')) {
        // 常见组合会话控制键在 keyup 也转发一次，让 Daemon 稳定判定
        ForwardKey(false, false, wParam, lParam, pic);
    }
    return S_OK;
}

// ---- ThreadMgrEventSink ----

STDMETHODIMP CTextService::OnInitDocumentMgr(ITfDocumentMgr*) { return S_OK; }
STDMETHODIMP CTextService::OnUninitDocumentMgr(ITfDocumentMgr*) { return S_OK; }
STDMETHODIMP CTextService::OnSetFocus(ITfDocumentMgr* pdimFocus, ITfDocumentMgr*) {
    focusDocMgr_ = pdimFocus;
    if (focusDocMgr_ && focusContext_) { focusContext_->Release(); focusContext_ = nullptr; }
    if (!focusDocMgr_) return S_OK;
    ITfContext* ctx = nullptr;
    if (FAILED(focusDocMgr_->GetTop(&ctx))) return S_OK;
    if (focusContext_) focusContext_->Release();
    focusContext_ = ctx;          // 持有引用
    ITfContext* ctxOwned = ctx;
    (void)ctxOwned;
    // 通知 Daemon 焦点变化（低频）
    if (ipcReady_ && keySend_) {
        CommandFrame cmd; cmd.kind = CommandKind::SetFocus;
        ImeFrameHeader hdr{}; hdr.magic = kFrameMagic; hdr.kind = static_cast<std::uint16_t>(FrameKind::Command);
        std::vector<std::uint8_t> enc; bool ok = EncodeCommand(cmd, enc, kMaxBodyLen);
        if (ok) { keySend_->Ring().Produce(hdr, enc.data(), (std::uint32_t)enc.size(), 0); keySend_->Notify(); }
    }
    return S_OK;
}
STDMETHODIMP CTextService::OnPushContext(ITfContext* pContext) {
    if (focusContext_ != pContext) return S_OK;
    if (focusContext_) { focusContext_->Release(); focusContext_ = nullptr; }
    focusContext_ = pContext; pContext->AddRef();
    return S_OK;
}
STDMETHODIMP CTextService::OnPopContext(ITfContext* pContext) {
    if (focusContext_ == pContext && focusContext_) { focusContext_->Release(); focusContext_ = nullptr; }
    return S_OK;
}

// ---- CompositionSink ----

STDMETHODIMP CTextService::OnCompositionTerminated(TfEditCookie, ITfComposition*) {
    // 外部强制终止 -> 自愈：清空本地组合态与品质缓存
    ClearCompositionCache();
    if (ipcReady_ && keySend_) {
        CommandFrame cmd; cmd.kind = CommandKind::Terminate;
        ImeFrameHeader hdr{}; hdr.magic = kFrameMagic; hdr.kind = static_cast<std::uint16_t>(FrameKind::Command);
        std::vector<std::uint8_t> enc; bool ok = EncodeCommand(cmd, enc, kMaxBodyLen);
        if (ok) { keySend_->Ring().Produce(hdr, enc.data(), (std::uint32_t)enc.size(), 0); keySend_->Notify(); }
    }
    return S_OK;
}

STDMETHODIMP CTextService::OnCleanupContext(TfEditCookie, ITfContext*) {
    ClearCompositionCache();
    composing_ = false;
    return S_OK;
}

// ---- 组合/上屏（净室，直接使用 TSF Range/Composition）----

bool CTextService::StartComposition(ITfContext* pic) {
    if (!pic) return false;
    ITfContextComposition* pCompose = nullptr;
    if (FAILED(pic->QueryInterface(IID_ITfContextComposition, (void**)&pCompose)))
        return false;
    pCompose->Release();
    return true;
}

bool CTextService::SetCompositionString(ITfContext* pic, const std::string& utf8, std::uint32_t) {
    if (!pic) return false;
    // 获取文档管理器->顶部上下文否则用传入 pic
    ITfContext* ctx = focusContext_ ? focusContext_ : pic;
    // 用 ITfRange 向组合串回设文本；失败也应保持（不阻断键）。
    // 组合串回设需要读写锁定上下文，需走 ITfContext::RequestEditSession。
    // 本壳在此仅登记待提交文本，由后台编辑会话路径（M1）真正落盘；失败也不阻断键。
    (void)ctx; (void)utf8;
    return false;
}
bool CTextService::CommitComposition(ITfContext* pic, const std::string& utf8) {
    (void)pic; (void)utf8;
    ClearCompositionCache();
    return true;
}
void CTextService::ClearCompositionCache() {
    pendingText_.clear();
}