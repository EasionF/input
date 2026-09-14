#include "tfs_service.h"
#include <ctffunc.h>
#include "../host/composition_kit.h"
#include "../ipc/ime_shared.h"
#include <new>
#include <string>
#include <vector>
#include <chrono>

using namespace netroom::ipc;
using netroom::host::HostActionKind;
using netroom::host::HostAction;
using netroom::host::Apply;

// ---- 8ms 裕度：Daemon 无响应即 passthrough，绝不阻塞宿主按键 ----
static constexpr DWORD kDaemonReplyBudgetMs = 8;

// 模块引用计数（由本编译单元导出的模块函数维护）

// ---------------- CTextService ----------------

CTextService::CTextService() = default;

CTextService::~CTextService() {
    if (threadMgr_) { threadMgr_->Release(); threadMgr_ = nullptr; }
    if (composition_) { composition_->Release(); composition_ = nullptr; }
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
        // 会话级通道名：与单个用户守护进程约定固定命名空间（单活动组合上下文）。
        // Host 侧一律为 connector；通道实体由 Daemon 创建并 init 头部。
        const std::wstring base = L"netroom_";

        keySend_ = std::make_unique<ImeChannel>(
            base + L"key", base + L"keyevt", RingBuffer::Role::Producer, /*isCreator*/false);
        candRecv_ = std::make_unique<ImeChannel>(
            base + L"cand", base + L"candevt", RingBuffer::Role::Consumer, /*isCreator*/false);
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
            HostAction act = Apply(cf);
            switch (act.kind) {
                case HostActionKind::UpdateComposition:   // 组合
                    composing_ = true;
                    StartComposition(pic);
                    SetCompositionString(pic, act.utf8Text, act.caret);
                    pendingText_ = act.utf8Text;
                    *pfEaten = TRUE;
                    break;
                case HostActionKind::Commit:              // 提交
                    CommitComposition(pic, act.utf8Text);
                    composing_ = false;
                    pendingText_.clear();
                    EndCompositionNow();
                    *pfEaten = TRUE;
                    break;
                case HostActionKind::ClearComposition:    // 放弃
                    composing_ = false;
                    pendingText_.clear();
                    EndCompositionNow();
                    *pfEaten = TRUE;
                    break;
                default:                                   // 放行
                    *pfEaten = FALSE;
                    break;
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

STDMETHODIMP CTextService::OnCompositionTerminated(TfEditCookie, ITfComposition* pComp) {
    // 外部强制终止 -> 自愈：清空本地组合态与品质缓存
    // 组合被外部/自身终止：释放该组合并清空本地指针与缓存
    if (composition_ == pComp) { composition_ = nullptr; }
    if (pComp) pComp->Release();
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

// ---- 组合/上屏：用 TSF 写编辑会话把候选帧落到目标窗口 ----
// 设计：组合语义（composition_kit）已独立且可无头单测；这里只剩 TSF 机制薄层。

namespace {
class CEditSession : public ITfEditSession {
public:
    CEditSession(void (*fn)(TfEditCookie, void*), void* ctx) : fn_(fn), ctx_(ctx) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfEditSession)) {
            *ppv = static_cast<ITfEditSession*>(this); AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&ref_)); }
    STDMETHODIMP_(ULONG) Release() override {
        LONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return static_cast<ULONG>(r);
    }
    STDMETHODIMP DoEditSession(TfEditCookie ec) override { fn_(ec, ctx_); return S_OK; }
private:
    void (*fn_)(TfEditCookie, void*);
    void* ctx_;
    LONG ref_ = 1;
};

// 0=提交  1=组合串更新  2=结束组合  3=仅起动组合
struct SessionJob {
    CTextService* svc;
    ITfContext* pic;
    int kind;
    std::wstring text;
};

void InsertCommitText(TfEditCookie ec, ITfContext* pic, const std::wstring& text) {
    ITfInsertAtSelection* pIns = nullptr;
    if (!pic || FAILED(pic->QueryInterface(IID_ITfInsertAtSelection, (void**)&pIns))) return;
    TF_SELECTION sel{};
    ULONG fetched = 0;
    if (pic->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched) == S_OK && fetched == 1) {
        ITfRange* outRange = nullptr;
        pIns->InsertTextAtSelection(ec, TF_IAS_NO_DEFAULT_COMPOSITION, text.c_str(),
                                    static_cast<LONG>(text.size()), &outRange);
        if (outRange) outRange->Release();
        sel.range->Release();
    }
    pIns->Release();
}

void EditSessionFn(TfEditCookie ec, void* v) {
    SessionJob* j = static_cast<SessionJob*>(v);
    switch (j->kind) {
        case 0:  // 提交：先结束组合，再在光标处插入文本
            j->svc->EndCompInSession(ec, j->pic);
            InsertCommitText(ec, j->pic, j->text);
            break;
        case 1:  // 组合串更新（若未起动会先起动）
            j->svc->UpdateCompInSession(ec, j->pic, j->text);
            break;
        case 2:  // 结束组合
            j->svc->EndCompInSession(ec, j->pic);
            break;
        case 3:  // 仅起动组合
            j->svc->StartCompInSession(ec, j->pic);
            break;
        default:
            break;
    }
}
}  // namespace

bool CTextService::RequestWriteSession(ITfContext* pic,
                                       void (*fn)(TfEditCookie, void*), void* ctx) {
    if (!pic || !fn) return false;
    CEditSession* es = new CEditSession(fn, ctx);
    HRESULT hrSession = E_FAIL;
    HRESULT hr = pic->RequestEditSession(clientId_, es, TF_ES_SYNC | TF_ES_READWRITE, &hrSession);
    es->Release();
    return SUCCEEDED(hr) && SUCCEEDED(hrSession);
}

bool CTextService::StartComposition(ITfContext* pic) {
    SessionJob job{this, pic, /*kind*/3, {}};
    return RequestWriteSession(pic, &EditSessionFn, &job);
}

bool CTextService::SetCompositionString(ITfContext* pic, const std::string& utf8, std::uint32_t) {
    std::wstring w;
    if (!netroom::host::Utf8ToWide(utf8, &w)) return false;
    SessionJob job{this, pic, /*kind*/1, w};
    return RequestWriteSession(pic, &EditSessionFn, &job);
}

bool CTextService::CommitComposition(ITfContext* pic, const std::string& utf8) {
    std::wstring w;
    netroom::host::Utf8ToWide(utf8, &w);
    SessionJob job{this, pic, /*kind*/0, w};
    return RequestWriteSession(pic, &EditSessionFn, &job) || w.empty();
}

void CTextService::EndCompositionNow() {
    if (!composition_) return;
    SessionJob job{this, focusContext_, /*kind*/2, {}};
    if (focusContext_) RequestWriteSession(focusContext_, &EditSessionFn, &job);
}

bool CTextService::StartCompInSession(TfEditCookie ec, ITfContext* pic) {
    if (composition_) return true;
    if (!pic) return false;
    TF_SELECTION sel{};
    ULONG fetched = 0;
    ITfRange* base = nullptr;
    if (pic->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched) == S_OK && fetched == 1)
        base = sel.range;
    else if (pic->GetStart(ec, &base) != S_OK)
        base = nullptr;
    if (!base) return false;
    ITfContextComposition* pCompose = nullptr;
    if (FAILED(pic->QueryInterface(IID_ITfContextComposition, (void**)&pCompose))) {
        base->Release(); return false;
    }
    HRESULT hr = pCompose->StartComposition(ec, base, static_cast<ITfCompositionSink*>(this),
                                            &composition_);
    pCompose->Release();
    base->Release();
    return SUCCEEDED(hr) && composition_ != nullptr;
}

bool CTextService::UpdateCompInSession(TfEditCookie ec, ITfContext* pic, const std::wstring& text) {
    if (!StartCompInSession(ec, pic)) return false;
    ITfRange* range = nullptr;
    if (composition_ && SUCCEEDED(composition_->GetRange(&range))) {
        range->SetText(ec, 0, text.c_str(), static_cast<LONG>(text.size()));
        range->Release();
    }
    return true;
}

bool CTextService::EndCompInSession(TfEditCookie, ITfContext*) {
    if (!composition_) return true;
    HRESULT hr = composition_->EndComposition(0);
    ClearCompositionPtr();
    return SUCCEEDED(hr);
}

void CTextService::ClearCompositionCache() {
    pendingText_.clear();
    // 不在此释放 composition_：组合生命周期由 StartComposition/EndComposition 与 OnCompositionTerminated 负责
}
