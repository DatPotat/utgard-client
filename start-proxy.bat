@echo off
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0bin\proxy-menu.ps1"
if errorlevel 1 pause