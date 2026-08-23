@echo off
python tools\validate_roformer_manifest.py
if errorlevel 1 exit /b 1
python tests\test_roformer_manifest.py
if errorlevel 1 exit /b 1
python tests\test_roformer_worker.py
if errorlevel 1 exit /b 1
python tests\test_roformer_cache.py
if errorlevel 1 exit /b 1
exit /b 0
