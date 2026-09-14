@echo off
setlocal
cd /d "%~dp0.."

set "DLL=%CD%\build-msvc\Release\netroom_tsf.dll"
if not exist "%DLL%" (
  echo [E] build first: build-msvc\Release\netroom_tsf.dll
  pause
  exit /b 1
)

echo == Register text service HKLM (UAC, click Yes) ==
powershell.exe -NoProfile -Command ^
  "Start-Process -Verb RunAs -FilePath 'regsvr32.exe' -ArgumentList '/s','"%DLL%"' -Wait"

echo == Refresh HKCU fallback ==
regsvr32 /s "%DLL%"

echo == Restart TSF host to refresh the input method list (no reboot) ==
taskkill /f /im ctfmon.exe >nul 2>&1
timeout /t 2 /nobreak >nul

echo == Start builtin pinyin daemon (ESC to quit) ==
start "netroom-daemon" cmd /k "cd /d build-msvc\Release && netroom_daemon.exe"

echo.
echo Now open Settings > Time ^& Language > Language > Keyboards and look for netroom.
echo If it is not there immediately, use Win+Space to refresh the switch list, or log off/on once.
echo Then press Win+Space to switch to netroom and type in Notepad.
endlocal