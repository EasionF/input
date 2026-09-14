@echo off
:: netroom dev launch: register the TSF text service (HKCU + HKLM) then run the daemon.
setlocal
cd /d "%~dp0.."

set DLL=%CD%\build-msvc\Release\netroom_tsf.dll
if not exist "%DLL%" (
  echo [E] DLL not found: build first.
  pause
  exit /b 1
)

echo == [1/2] Register text service (HKCU, current user) ==
regsvr32 /s "%DLL%"
if errorlevel 1 echo    HKCU register failed.

echo == [2/2] Register HKLM (all-users) via UAC -- click "Yes" on the prompt ==
powershell.exe -NoProfile -Command "Start-Process -Verb RunAs -FilePath 'regsvr32.exe' -ArgumentList '/s','""%DLL%""' -Wait"
if errorlevel 1 echo    HKLM register failed (maybe declined UAC).

echo.
echo Starting daemon (builtin pinyin engine; ESC to quit)...
start "netroom-daemon" cmd /k "cd /d build-msvc\Release & chcp 65001 >nul & netroom_daemon.exe"

echo.
echo Next: open Settings > Time & Language > Language > Keyboards > Add a keyboard.
echo If netroom does not appear immediately, SIGN OUT and back in (TSF TIP list refreshes on logon).
echo Then Win+Space to switch to netroom and type pinyin (e.g. "ni") in Notepad.
endlocal