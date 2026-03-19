@echo off
setlocal

:: Enable ANSI escape sequences in Windows 10
reg add HKCU\Console /v VirtualTerminalLevel /t REG_DWORD /d 1 /f >nul 2>&1

:: ESP-IDF paths
set IDF_PATH=D:\Espressif\frameworks\esp-idf-v5.3.2
set PYTHON=D:\Espressif\python_env\idf5.3_py3.11_env\Scripts\python.exe

:: Serial port settings
set PORT=COM3
set BAUD=115200

echo ==========================================
echo MimiClaw Serial Monitor
echo ==========================================
echo Port: %PORT%
echo Baud: %BAUD%
echo.
echo Press Ctrl+] to exit
echo ==========================================
echo.

"%PYTHON%" -m serial.tools.miniterm --encoding=utf-8 "%PORT%" %BAUD%

pause
