# netroom 自测指南（内置拼音引擎）

当前仓库已把整条链路做好，可用**内置拼音引擎**直接打字测试（无 librime 也能跑）：
TSF 文本服务(DLL) ⇄ 共享内存 ⇄ 独立守护进程(引擎) ⇄ 组合/上屏。

## 一、直接测（推荐）
0. 前置：已用 VS2022 BuildTools 构建 Release（本仓库已构建好）。
1. 双击 `tools\run_dev.bat`
   - 自动 `regsvr32` 注册文本服务（HKCU，免管理员）
   - 自动启动 `netroom_daemon.exe`（内置拼音引擎，新开窗口，按 ESC 退出）
2. 任意输入框/记事本里按 `Win+空格` 或点任务栏输入法，切到 **netroom**。
3. 输入拼音（如 `ni`、`hao`、`zhong`），v1 会：
   - 组合串显示；`空格` 或数字键上屏首选/对应候选；
   - `退格` 删组合、`Esc` 放弃。

> 若输入法列表里没出现 netroom：Win 设置 → 时间和语言 → 输入 → 语言选项 →
> 键盘 → 添加键盘 → 找 netroom，或在“高级键盘设置”里勾选“输入语言栏”。

## 二、命令行手动流程（等价）
```
cd build-msvc\Release
regsvr32 /s netroom_tsf.dll
netroom_daemon.exe        # 保持运行；ESC 退出
```
切到 netroom 后开始打字。

## 三、说明与边界（诚实清单）
- **当前引擎是内置拼音**（`netroom/daemon/pinyin_engine.cpp`）：词库较小，属可用的落底实现，
  用于无 librime 时的端到端跑通与联调。
- **librime 真引擎**：设计为 `IEngine` 可插入接口，已留 rime 适配位；librime 需在桌面环境
  用其自带的 `build.bat deps` + `build.bat librime`（依赖 Boost 与正常网络）构建后替换。
- **TSF 组合/上屏**（`netroom/tfs_host/tfs_service.cpp`）：编辑会话 + `ITfInsertAtSelection`
  已接入并通过编译；真实击键/组合 UI 需在本机人工验证 —— 即本页“一、直接测”。
- 全部无头可测项已由 `ctest` 覆盖（ring/channel/codec/pinyin/daemon_integration/composition_kit）。

## 四、回归
```
cmake --build build-msvc --config Release
ctest --test-dir build-msvc -C Release
```