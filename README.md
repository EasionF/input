# netroom 输入法

一款以 librime 为拼音引擎、净室自研 TSF 前端壳的 Windows 离线输入法。

## 设计
- 双进程：TL 宿主(TextService.dll)仅按键拦截/组合/上屏；Daemon.exe 承载引擎/词库/渲染/AI。
- IPC：共享内存无锁环形缓冲区 + 命名管道控制面，端到端零拷贝，8ms 超时熔断直通。
- 词库：librime 基座 + 三层隔离词库(基础/自适应/热词沙盒)。
- 详见 `docs/`。

## 构建前置
- Visual Studio 2022（MSVC + Windows SDK）。当前环境仅 MinGW，无法编译 TSF/librime 部分。
- vcpkg 安装 librime。
- 纯逻辑层（IPC ring buffer / crc / 帧校验）可用 g++ 独立验证。

## 目录
- `netroom/ipc`    跨进程通信契约与环形缓冲区
- `netroom/tfs_host`  TSF 宿主壳（待 MSVC）
- `netroom/daemon`   引擎侧（待 MSVC + librime）
- `tests`            单元测试
