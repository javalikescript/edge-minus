@echo off
setlocal

set "SCRIPT=%~dp0install.ps1"
if not exist "%SCRIPT%" (
  echo install.ps1 was not found next to install.bat.
  exit /b 1
)

where powershell.exe >nul 2>nul
if errorlevel 1 (
  echo powershell.exe was not found.
  exit /b 1
)

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT%" %*
exit /b %ERRORLEVEL%
