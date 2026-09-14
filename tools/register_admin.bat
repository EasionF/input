@echo off
:: Register / unregister netroom as an all-user (HKLM) TSF text service. Requires admin (UAC).
cd /d "%~dp0.."
set DLL=%CD%\build-msvc\Release\netroom_tsf.dll
if not exist "%DLL%" (echo DLL not found: %DLL% & pause & exit /b 1)
if "%1"=="u" (
  powershell.exe -NoProfile -Command "Start-Process -Verb RunAs -FilePath 'regsvr32.exe' -ArgumentList '/s','/u','""%DLL%""' -Wait"
  echo Unregistered HKLM (if UAC approved).
) else (
  powershell.exe -NoProfile -Command "Start-Process -Verb RunAs -FilePath 'regsvr32.exe' -ArgumentList '/s','""%DLL%""' -Wait"
  echo Registered HKLM (if UAC approved).
)
echo If the input method still is not listed, SIGN OUT and back in once.
pause