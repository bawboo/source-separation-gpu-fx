@echo off
rem Double-click: start the walk-away loop driver in a minimized window.
rem The minimized window must stay open (minimized is fine). Screen can be off.
rem Laptop sleep pauses the loop - keep the machine awake (plugged in).
cd /d "%~dp0.."
start "HTFX RoFormer Loop" /min "C:\Program Files\Git\bin\bash.exe" -c "bash .loop/run_loop.sh >> .loop/nohup.out 2>&1"
echo Loop driver started in a minimized window titled "HTFX RoFormer Loop".
echo   - Progress:  .loop\journal.md  /  .loop\driver.log  /  .loop\backlog.json
echo   - Stop:      double-click .loop\LOOP_STOP.cmd  (or close the minimized window)
pause
