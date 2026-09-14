@echo off
:: netroom 开发自测一键启动：注册净室 TSF 文本服务 + 启动内置拼音引擎守护进程。
:: HKLM 注册需管理员 —— 会弹出 UAC 窗口，请点“是”。
setlocal
cd /d "%~dp0.."

set DLL=%CD%\build-msvc\Release\netroom_tsf.dll
if not exist "%DLL%" (
  echo [E] 未找到 %DLL% ：请先用 MSVC 构建 Release。
  pause
  exit /b 1
)

echo == [1/2] 注册文本服务（HKLM 全用户 + HKCU；会弹 UAC，请点是）==
powershell.exe -NoProfile -Command "Start-Process -Verb RunAs -FilePath 'regsvr32.exe' -ArgumentList '/s','""%DLL%""' -Wait"
rem 上面的管理员注册若成功，HKLM 已写入；此处再补一次 HKCU（免管理员）注册。
regsvr32 /s "%DLL%"
echo       注册完成

echo == [2/2] 启动守护进程（内置拼音引擎，按 ESC 退出）==
start "netroom-daemon" cmd /k "cd /d build-msvc\Release & chcp 65001 >nul & netroom_daemon.exe"

echo.
echo 接下来：打开“设置->时间和语言->语言->键盘(或输入)”，应能看到 netroom。
echo 选中后切到 netroom（Win+空格 / 任务栏输入法），在记事本里输入拼音 ni，
echo 空格 或 数字键 上屏。按 ESC 退出守护进程结束本次测试。
echo 若输入法列表仍没有：请注销再登录一次（TSF 列表常驻缓存）。
endlocal