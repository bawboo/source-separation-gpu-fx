@echo off
rem cheap verification tier — every iteration (LOOP_PLAN §3)
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -no_logo -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1
cd /d "%~dp0..\.."
cmake --build build\windows-installed --config Release --target HTDemucsGpuFX_Standalone htdemucs_ui_configuration_smoke -- /m
if errorlevel 1 exit /b 1
build\windows-installed\Release\htdemucs_ui_configuration_smoke.exe
if errorlevel 1 exit /b 1
rem extension point: the loop may create/extend cheap_extra.cmd (add checks only)
if exist "%~dp0cheap_extra.cmd" (
  call "%~dp0cheap_extra.cmd"
  if errorlevel 1 exit /b 1
)
exit /b 0
