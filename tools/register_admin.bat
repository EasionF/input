@echo off
setlocal
cd /d "%~dp0.."

set "DLL=%CD%\build-msvc\Release\netroom_tsf.dll"
if not exist "%DLL%" (
  echo DLL not found: build first and retry.
  pause
  exit /b 1
)

if /i "%1"=="u" (
  echo Unregister HKLM (UAC prompt -> click Yes)...
  powershell.exe -NoProfile -Command "Start-Process -Verb RunAs -FilePath 'regsvr32.exe' -ArgumentList '/s','/u','""%DLL%""' -Wait"
) else (
  echo Register HKLM (UAC prompt -> click Yes)...
  powershell.exe -NoProfile -Command "Start-Process -Verb RunAs -FilePath 'regsvr32.exe' -ArgumentList '/s','""%DLL%""' -Wait"
)

echo Restart TSF host to refresh list...
taskkill /f /im ctfmon.exe >nul 2>&1
timeout /t 2 /nobreak >nul

echo Done. If netroom is still missing in keyboard list, sign out and back in once, then retry.
endlocal