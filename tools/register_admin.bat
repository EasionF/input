@echo off
:: 管理员方式（重）注册/反注册 netroom 文本服务（写 HKLM 需要管理员 UAC）。
if "%1"=="u" goto unreg
cd /d "%~dp0.."
echo 正在以管理员身份注册（如弹出 UAC 请点“是”）...
powershell.exe -NoProfile -Command "Start-Process -Verb RunAs -FilePath 'regsvr32.exe' -ArgumentList '/s','""%CD%\build-msvc\Release\netroom_tsf.dll""' -Wait"
echo 完成。若输入法列表未更新，请注销再登录，或进 设置-时间和语言-语言-键盘 添加。
pause
exit /b 0
:unreg
cd /d "%~dp0.."
echo 正在以管理员身份反注册...
powershell.exe -NoProfile -Command "Start-Process -Verb RunAs -FilePath 'regsvr32.exe' -ArgumentList '/s','/u','""%CD%\build-msvc\Release\netroom_tsf.dll""' -Wait"
pause
exit /b 0