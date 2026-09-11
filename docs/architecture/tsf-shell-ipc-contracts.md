# 净室 TSF 前端壳 — COM 接口契约 与 共享内存结构体设计

> 目标：TL 宿主（TextService.dll）仅负责「按键拦截 → 转发 Daemon → 拿回候选/提交串」，
> 全程零逻辑、零分配所有权、零自研状态。所有契约以「够跑通最小闭环且宿主不崩」为准，
> 在净室 Host 与 Daemon 之间的 IPC 完全脱离 JSON，走共享内存环形缓冲区。
> 文档中所有 GUID 常量以本机 SDK 的 msctf.h / tsf.h 为准（`Get-Content` 核对），
> 禁止凭记忆硬编码。

## 1. 生命周期总览

```
DllGetClassObject(clsid)
  ->  TSF 管理器为 TextService 创建实例
       -> ITfTextInputProcessorEx::ActivateEx(ptim, tid, dwFlags)
            -> 注册 ThreadMgrEventSink (焦点/上下文跟踪)
            -> 建立 IPC 连接（连 Daemon，握手 ABI）
       -> 活动期间 -> KeyEventSink::OnKeyDown/OnKeyUp
            -> 转发 Daemon -> Daemon 返回候选帧 -> 走 Composition/Commit
       -> ITfTextInputProcessorEx::Deactivate
            -> 注销 Sink、断 IPC
```

崩溃面：TL 侧所有导出函数/COM 回调外层套 SEH + `noexcept` 屏障，
任何 C++ 异常禁止逸出到宿主；IPC 8ms 硬超时，超时即降级 Pure Passthrough。

## 2. 四个核心 COM 接口契约

### 2.1 ITfTextInputProcessorEx（入口 / 生命周期）
接口职责：TSF 管理器挂载/卸载本输入法。
```
HRESULT ActivateEx(ITfThreadMgr *ptim,
                   TID          tid,          // 本服务线程 id
                   DWORD        dwFlags);     // TIP_UI_ELEMENT_ENABLED 等
HRESULT Deactivate();
```
- ActivateEx 成功后才允许注册 sink 与连 IPC；失败必须返回失败 HRESULT 并彻底回滚。
- Deactivate 必须同步销毁所有 sink 注册与 IPC 句柄，幂等（可被调用多次）。
- 契约：TL 不持界面元素；UI 由 Daemon 拥有。

### 2.2 ITfKeyEventSink（键击热路径）
接口职责：接收宿主窗口的按键事件，决定「吞键（组合）或放行（passthrough）」。
```
HRESULT OnKeyDown(ITfContext *pInput, WPARAM wParam, LPARAM lParam, BOOL *pfEaten);
HRESULT OnKeyUp  (ITfContext *pInput, WPARAM wParam, LPARAM lParam, BOOL *pfEaten);
HRESULT SetFocus (BOOL fForeground);
HRESULT OnTestKeyDown(ITfContext *pInput, WPARAM wParam, LPARAM lParam, BOOL *pfEaten);
HRESULT OnTestKeyUp  (ITfContext *pInput, WPARAM wParam, LPARAM lParam, BOOL *pfEaten);
```
- OnKeyDown 为真实决策点：先查 8ms 超时，超时一律 `*pfEaten=FALSE` 放行。
- 仅当 Daemon 确认该键进入组合时置 `*pfEaten=TRUE`；其余一律放行。
- 命中「游戏静默/全屏」豁免名单时整段跳过（不放行也不组合，纯物理扫描码直通）。

### 2.3 ITfThreadMgrEventSink（焦点 / 上下文跟踪）
接口职责：感知激活的上下文（ITfContext），把 focus change 同步给 Daemon，
作为每应用输入状态记忆与游戏静默的依据。
```
HRESULT OnInitDocumentMgr(ITfDocumentMgr *pDocMgr);
HRESULT OnUninitDocumentMgr(ITfDocumentMgr *pDocMgr);
HRESULT OnSetFocus(ITfDocumentMgr *pDocMgrFocus, ITfDocumentMgr *pDocMgrPrevFocus);
HRESULT OnPushContext(ITfContext *pContext);
HRESULT OnPopContext(ITfContext *pContext);
```
- TL 必须保存「当前焦点上下文」指针，供 KeyEventSink 与 TextEditSink 定位。
- 焦点变化通过 IPC `CmdSetFocus{ hwnd, title, exe }` 同步 Daemon（低频控制通道）。

### 2.4 ITfTextEditSink（文本变更 / 组合同步）
接口职责：接收组合/上屏阶段文档变更，向 Daemon 反馈，驱动「误删负反馈」与 CSS。
```
HRESULT OnEndEdit(ITfContext *pContext,
                  TfEditCookie ecReadOnly,
                  ITfEditRecord *pEditRecord);
```
- OnEndEdit 读取 pEditRecord 中的变更范围，判别是「本输入法提交」还是「外部输入」，
  仅后者才纳入负反馈判定（防止自我循环误伤）。
- 该回调绝不阻塞；只向 Daemon 投递异步通知，不在此处做重逻辑。

**必配但不再展开的胶水接口**（TL 正确工作的前提，随 M0 基建落实）：
- `ITfCompositionSink::OnCompositionTerminated`：外部强制终止组合时清理内部状态。
- `ITfCleanupContextSink::OnCleanupContext`：目标进程异常清理时的自愈钩子。
- TSF 类别注册（Category `CATID_TF_TSERVICE`）+ `DllGetClassObject` 类工厂导出。

## 3. 共享内存契约（端到端零拷贝）

### 3.1 通道模型
- **单向共享内存 ×2**（CreateFileMapping）：
  - Host→Daemon：按键/命令（低吞吐、高实时）。
  - Daemon→Host：候选帧/提交/终止（中吞吐）。
- 每条通道 = 预分配**环形缓冲区** + 1 个 Win32 Event（对应方向）。
- 字节布局使用固定 POD / 显式 offset 对齐，禁止 STL / vtable / 指针跨进程。

### 3.2 共享内存总体布局
```
CreateFileMapping(nFileSizeHigh=0, nFileSizeLow)
  -----------------------------------------------------------------
  |  SharedRingHeader（每通道固定 64B, 对齐 64）                    |
  |  Slot[0]  Slot[1] ... Slot[kMaxSlots-1]                        |
  |   每 Slot = IpcFrameHeader(64B, 对齐64) + Payload(max 16KB)    |
  -----------------------------------------------------------------
```
每个方向 map 一次；"Host→Daemon" 与 "Daemon→Host" 为两块独立映射 +
各自命名句柄与事件，互不共享写位。

### 3.3 SharedRingHeader
```cpp
// 固定 64 字节，C 布局，不可变
#define IME_IPC_MAGIC      0x494D4521u  // "IME!"
#define IME_IPC_ABI_VERSION 1u
#define IME_IPC_MAX_SLOTS   16u
#define IME_IPC_SLOT_BYTES  16384u      // 单槽负载容量

typedef struct ImeSharedRingHeader {
    uint32_t magic;        // 0x494D4521   [0]
    uint32_t abiVersion;   // =1           [4]
    uint32_t producer;     // 生产者当前写槽索引   [8]
    uint32_t consumer;     // 消费者当前读槽索引   [12]
    uint32_t writeSeq;     // 单调递增写序号        [16]
    uint32_t state;        // Alive/Eof/Error      [20]
    uint32_t slotCount;    // 常量=16              [24]
    uint32_t slotBytes;    // 常量=16384           [28]
    uint32_t pad[8];       // 对齐到 64B           [32..63]
} ImeSharedRingHeader;                            // sizeof == 64

typedef enum ImeRingState {
    IME_RING_ALIVE = 0,
    IME_RING_EOF   = 1,
    IME_RING_ERROR = 2,
    IME_RING_READY = 3
} ImeRingState;
```

### 3.4 IpcFrameHeader + Payload 前缀
```cpp
#define IME_FRAME_MAGIC 0x4652414Du  // "FRAM"

typedef struct ImeFrameHeader {
    uint32_t magic;         // 0x4652414D   [0]
    uint32_t length;        // payload 字节数（不含本头）  [4]
    uint16_t kind;          // 见 ImeFrameKind             [8]
    uint16_t flags;         // 保留位/位标志               [10]
    uint32_t frameSeq;      // 帧序号                        [12]
    uint64_t ts;            // 单调时钟                       [16]
    uint32_t pad[10];       // 对齐至 64B                    [24..63]
} ImeFrameHeader;                                     // sizeof == 64

typedef enum ImeFrameKind {
    IME_FRAME_KEY          = 1,  // Host->Daemon 键击
    IME_FRAME_CMD          = 2,  // Host->Daemon 控制（focus/清理）
    IME_FRAME_CANDIDATE    = 3,  // Daemon->Host 候选帧
    IME_FRAME_ACTION       = 4,  // Daemon->Host 提交/终止/替换
    IME_FRAME_FEEDBACK     = 5   // Daemon->Host 取证/回执（可选）
} ImeFrameKind;
```

### 3.5 键击帧 Payload（Host→Daemon）
```cpp
typedef struct ImeKeyPayload {
    uint32_t vk;          // 虚拟键码（IO_REMOVABLE 层 raw）
    uint32_t scan;        // 扫描码
    uint32_t flags;       // 位：#1 down,#2 up,#3 repeat,#4 extended
    uint32_t hwnd;        // 目标窗口句柄（会话内全局有效）
    uint32_t threadId;    // 宿主线程 id
    uint32_t preserved;   // KeyState 保留位
} ImeKeyPayload;          // sizeof == 24
```

### 3.6 候选帧 Payload（Daemon→Host）
候选集合以「两张表」编码，均从 payload 段随帧传输：
```cpp
// 候选条目表：定长头
typedef struct ImeCandidateEntry {
    uint16_t kind;        // ImeCandidateKind
    uint16_t label;       // 候选序号 1..n
    uint16_t compLen;     // 提交时回退的组合串字节数
    uint16_t textOff;     // 文本串表偏移
    uint16_t textLen;     // 文本串长度
    uint16_t auxOff;      // 辅助说明（英文/注释）偏移, 0=无
    uint16_t auxLen;
    uint16_t reserved;
} ImeCandidateEntry;      // sizeof == 16

// 候选帧负载布局
// [0..n*16)          ：ImeCandidateEntry 数组，n 由帧头 length 推导
// [n*16 ... textStart)：UTF-8 文本串表（候选正文）
// [... auxStart]       ：UTF-8 辅助说明表
typedef enum ImeCandidateKind {
    IME_CAND_ASCII    = 0,
    IME_CAND_HANZI    = 1,
    IME_CAND_ENGLISH  = 2,
    IME_CAND_EMOJI    = 3,
    IME_CAND_JYUTPING = 4     // 粤拼读音
} ImeCandidateKind;
```
- 同一帧内文本串/辅助串各一张连续 UTF-8 表，条目用 offset 引用，避免逐条分配。
- 帧失败/超限时 Daemon 只发合法前缀；Host 校验 length 不越界，越界丢弃整帧。

### 3.7 环形缓冲区无锁读写算法
- 单生产者 / 单消费者（每方向固定），无锁：
  - Producer：写 `producer` 槽数据 → 写 barrier → 递增 `writeSeq` → SetEvent。
  - Consumer：读 `consumer` 槽 → 校验 magic/length/seq → 处理 → 递增 `consumer`。
  - 满/空判定：`(consumer+1) % slotCount == producer` 视为满（留一空槽防覆盖）。
- 用 `InterlockedIncrement`/`std::atomic<uint32_t>`（release/acquire）保证可见性；
- 事件句柄用 `CreateEvent`，命名管道路径 `\\.\pipe\ime_<pid>`（控制面/握手不依赖事件）。

### 3.8 控制面（命名管道，低频）
- 仅承载：握手/ABI 校验、`CmdSetFocus`、`CmdSetExemptList`、`CmdReset`。
- 所有命令走同一契约：`{ cmd, seq, params }`，响应必须有超时（8ms）且可重连。

## 4. 降级与防御清单（对应本工程既有兜底策略）
1. SEH + noexcept 全屏障，CRT 异常一律转为 `E_FAIL` + 放行键击。
2. IPC 8ms 硬超时 → Pure Passthrough；恢复条件：Daemon 心跳恢复、切换焦点。
3. 原地改写分级：仅标准 ITfRange/UIA TextPattern 控件做原地替换，其余降级为
   「悬浮卡片 + 复制进剪贴板」。
4. 软性负反馈 ×0.85 起步 + 白名单（基础高频 / 手动翻页确认 / 加星词）免斥。
5. WebView2 渲染不可信 LLM 内容默认沙箱 + 禁外联脚本 + 禁本地文件访问；
   敏感数据 Egress Filter 仅拦截出站，绝不阻断本地编辑。

## 5. 落地交付物（与此契约对应的 M0/M1 验收）
- `netroom/ipc/ime_shared.h`          —— 3.3/3.4/3.5/3.6 结构体 + 常量
- `netroom/ipc/ring_buffer.h/.cpp`    —— 无锁环形通道（两方向复用）
- `netroom/tfs_host/tldllmain.cpp`    —— DllGetClassObject + Activity
- `netroom/tfs_host/tl_sinks.cpp`     —— 四 sink 实现 + 胶水
- `netroom/tfs_host/tl_ipc_client.cpp`—— 共享内存 + 8ms 超时 + passthrough 降级
- 单元测试：`test_ring_buffer.cpp`（满/空/覆盖/乱序）、`test_frame_parse.cpp`
- 每个接口契约在上线前必须有对��单测覆盖，无占位符。
