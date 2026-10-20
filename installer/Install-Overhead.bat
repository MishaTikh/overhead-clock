@echo off
rem Overhead one-click installer: double-click, or drop a firmware .bin onto this file.
title Overhead installer
if "%~1"=="" (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-overhead.ps1"
) else (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-overhead.ps1" -Firmware "%~1"
)
