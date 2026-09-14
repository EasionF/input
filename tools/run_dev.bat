@echo off
setlocal
cd /d "%~dp0.."

set DLL=%CD%\build-msvc\Release\netroom_tsf.dll
if not exist "%DLL%" (
  echo DLL not found; build first.
  pause
  exit /b 1
)

echo Step 1 of 2: Register text service for current user (HKCU).
regsvr32 /s "%DLL%"

echo Step 2 of 2: Register for all users (HKLM) - a UAC prompt will pop. Click Yes.
powershell.exe -NoProfile -Command "Start-Process -Verb RunAs -FilePath 'regsvr32.exe' -ArgumentList '/s','""%DLL%""' -Wait"

echo Refresh TSF list now.
taskkill /f /im ctfmon.exe >nul 2>&1
timeout /t 2 /nobreak >nul

echo Start builtin pinyin daemon (ESC to quit).
start "netroom-daemon" cmd /k "cd /d build-msvc\Release && netroom_daemon.exe"

echo.
echo Open Settings, then Time and Language, then Language, then Keyboards.
echo If netroom is not there, Win+Space once or sign out and back in once.
endlocal