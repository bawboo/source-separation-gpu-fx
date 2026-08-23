@echo off
setlocal
rem Codex and a few launchers can supply both PATH and Path. MSBuild treats
rem that as duplicate case-insensitive dictionary keys and refuses to start CL.
set "HTFX_BUILD_PATH=%PATH%"
set "PATH="
set "Path="
set "PATH=%HTFX_BUILD_PATH%"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo Visual Studio Installer vswhere.exe was not found. 1>&2
  exit /b 2
)
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT (
  echo Visual Studio C++ Build Tools were not found. 1>&2
  exit /b 3
)
call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -no_logo -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_windows_installed.ps1" %*
exit /b %errorlevel%
