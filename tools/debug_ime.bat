@echo off
setlocal
cd /d "%~dp0.."

set "DLL=%CD%\build-msvc\Release\netroom_tsf.dll"
if not exist "%DLL%" (
  echo Build first: build-msvc\Release\netroom_tsf.dll
  pause
  exit /b 1
)

echo (1/4) Register HKLM - a UAC prompt pops. Click Yes.
powershell.exe -NoProfile -Command ^
  "Start-Process -Verb RunAs -FilePath 'regsvr32.exe' -ArgumentList '/s','"%DLL%"' -Wait"

echo (2/4) Register HKCU.
regsvr32 /s "%DLL%"

echo (3/4) Refresh TSF/input stack now (no reboot): restart ctfmon and explorer.
taskkill /f /im ctfmon.exe >nul 2>&1
timeout /t 1 /nobreak >nul
taskkill /f /im explorer.exe >nul 2>&1
timeout /t 3 /nobreak >nul

echo (4/4) Start builtin pinyin engine daemon (press ESC in any window quits it).
start "" /D "%CD%\build-msvc\Release" netroom_daemon.exe

echo.
echo Done. Now press Win+Space, or click the input indicator at bottom-right,
echo and choose netroom. Type pinyin (ni) in Notepad, space/number to commit.
echo If netroom is still absent: Settings > Time & Language > Language > (your language)
echo   > Keyboards > Add a keyboard > netroom. As a last resort, sign out and back in once.
endlocal