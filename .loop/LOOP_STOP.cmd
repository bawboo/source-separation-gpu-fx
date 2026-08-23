@echo off
rem Double-click: gracefully stop the loop before its next iteration
type nul > "%~dp0STOP"
echo STOP file created - the driver will abort cleanly before the next launch.
pause
