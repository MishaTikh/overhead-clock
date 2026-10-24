@echo off
rem Overhead installer: double-click to open the installer window, or drop a firmware .bin onto this file.
title Overhead installer
if "%~1"=="" (
  powershell -NoProfile -ExecutionPolicy Bypass -STA -WindowStyle Hidden -File "%~dp0install-overhead.ps1"
) else (
  powershell -NoProfile -ExecutionPolicy Bypass -STA -WindowStyle Hidden -File "%~dp0install-overhead.ps1" -Firmware "%~1"
)
