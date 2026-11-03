@echo off
rem Double-click: quits Faider, builds the latest code and starts it again (tools\restart-faider.ps1).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\restart-faider.ps1"
