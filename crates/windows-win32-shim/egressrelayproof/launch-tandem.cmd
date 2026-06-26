@echo off
rem Copyright (c) Microsoft Corporation.
rem
rem Launch the two validation-tier services in tandem (merriam + wordy's aliased
rem relay) under a chosen egress mode, for manual exploration. Thin wrapper over
rem launch-tandem.ps1.
rem
rem Usage:
rem   launch-tandem.cmd [passthrough|redirect|buffer|replay] [debug|release]
rem
rem Examples:
rem   launch-tandem.cmd                 (passthrough, debug)
rem   launch-tandem.cmd buffer          (buffer mode, debug)
rem   launch-tandem.cmd redirect release

setlocal
set "MODE=%~1"
if "%MODE%"=="" set "MODE=passthrough"
set "CONFIG=%~2"
if "%CONFIG%"=="" set "CONFIG=debug"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0launch-tandem.ps1" -Mode %MODE% -Configuration %CONFIG%
endlocal
