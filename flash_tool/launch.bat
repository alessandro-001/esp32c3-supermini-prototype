@echo off
title ESP32 Firmware Flasher
set "SCRIPT_DIR=%~dp0"
set "PYTHON=%SCRIPT_DIR%..\penv\Scripts\python.exe"
if not exist "%PYTHON%" (
    set "PYTHON=C:\Users\aless\AppData\Local\Programs\Python\Python314\python.exe"
)
"%PYTHON%" -m pip install pyserial --quiet
"%PYTHON%" "%SCRIPT_DIR%flash.py"
pause
