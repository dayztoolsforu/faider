@echo off
rem Double-click: quits Patchbay Console, builds the latest code and starts it again (tools\restart-patchbay-console.ps1).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\restart-patchbay-console.ps1"
