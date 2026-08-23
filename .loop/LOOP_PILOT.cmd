@echo off
rem Double-click: run ONE pilot iteration in this window (watch it work)
cd /d "%~dp0.."
"C:\Program Files\Git\bin\bash.exe" -c "MAX_LAUNCHES=1 bash .loop/run_loop.sh"
echo.
echo ===== pilot finished - check .loop\iterations\ , .loop\journal.md , git log =====
pause
