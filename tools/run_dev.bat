@echo off
:: netroom 开发自测一键启动：注册净室 TSF 文本服务 + 启动内置拼音引擎守护进程。
setlocal
cd /d "%~dp0.."

set DLL=build-msvc\Release\netroom_tsf.dll
if not exist "%DLL%" (
  echo [E] 未找到 %DLL% ：请先用 MSVC 构建 Release。
  pause
  exit /b 1
)

echo == [1/2] 注册文本服务（HKCU，免管理员）==
regsvr32 /s "%DLL%"
if errorlevel 1 ( echo 注册失败 & pause & exit /b 1 )
echo       OK

echo == [2/2] 启动守护进程（内置拼音引擎，按 ESC 退出）==
start "netroom-daemon" cmd /k "cd /d build-msvc\Release & chcp 65001 >nul & netroom_daemon.exe"

echo.
echo 接下来：在记事本里，按 Win+空格 或 任务栏输入法，切到 netroom，
echo 输入拼音（如 ni），候选/组合应显示，空格 或 数字键 上屏。
echo 按 ESC 退出守护进程结束本次测试。
endlocal