@echo off
rem full verification tier — every 5 iterations and ALWAYS before declaring convergence (LOOP_PLAN §3)
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -no_logo -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1
cd /d "%~dp0..\.."
cmake --build build\windows-installed --config Release --target HTDemucsGpuFX_Standalone htfx_hardware_probe htdemucs_record_mode_smoke htdemucs_media_io_smoke htdemucs_ui_configuration_smoke -- /m
if errorlevel 1 exit /b 1
echo === ui_configuration_smoke ===
build\windows-installed\Release\htdemucs_ui_configuration_smoke.exe
if errorlevel 1 exit /b 1
echo === media_io_smoke ===
build\windows-installed\Release\htdemucs_media_io_smoke.exe
if errorlevel 1 exit /b 1
echo === record_mode_smoke (auto/GPU) ===
build\windows-installed\Release\htdemucs_record_mode_smoke.exe
if errorlevel 1 exit /b 1
rem roformer smoke: mandatory once the target exists (backlog A8 tracks its creation)
cmake --build build\windows-installed --config Release --target htdemucs_roformer_smoke -- /m >nul 2>&1
if exist build\windows-installed\Release\htdemucs_roformer_smoke.exe (
  echo === roformer_smoke ===
  build\windows-installed\Release\htdemucs_roformer_smoke.exe
  if errorlevel 1 exit /b 1
) else (
  echo [full] htdemucs_roformer_smoke not built yet - A8 still open
)
rem extension point: the loop may create/extend full_extra.cmd (add checks only)
if exist "%~dp0full_extra.cmd" (
  call "%~dp0full_extra.cmd"
  if errorlevel 1 exit /b 1
)
exit /b 0
