@echo off
setlocal
cd /d "%~dp0.."

set "DLL=%CD%\build-msvc\Release\netroom_tsf.dll"
if not exist "%DLL%" (
  echo Build first: build-msvc\Release\netroom_tsf.dll
  pause
  exit /b 1
)

echo (1/3) Register HKLM - a UAC prompt will pop. Click Yes.
powershell.exe -NoProfile -Command ^
  "Start-Process -Verb RunAs -FilePath 'regsvr32.exe' -ArgumentList '/s','"%DLL%"' -Wait"

echo (2/3) Register HKCU fallback.
regsvr32 /s "%DLL%"

echo (3/3) Restart TSF host so the input list refreshes now (no reboot).
taskkill /f /im ctfmon.exe >nul 2>&1
timeout /t 2 /nobreak >nul

echo Start builtin pinyin daemon (ESC to quit).
start "netroom-daemon" cmd /k "cd /d build-msvc\Release && netroom_daemon.exe"

echo.
echo Done. Open Settings, then Time and Language, then Language, then Keyboards.
echo If netroom is not listed yet, press Win+Space once; otherwise sign out and back in.
echo Then Win+Space to switch to netroom and type pinyin (say ni) in Notepad.
endlocal