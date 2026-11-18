@echo off
setlocal EnableExtensions
cd /d "%~dp0"
set "PMB_WORKER="
if exist "build\MotionBlurWorker.exe" set "PMB_WORKER=build\MotionBlurWorker.exe"
if not defined PMB_WORKER if exist "bin\MotionBlurWorker.exe" set "PMB_WORKER=bin\MotionBlurWorker.exe"
if not defined PMB_WORKER goto build
echo Using prebuilt %PMB_WORKER%. Skipping Build.cmd.
goto install
:build
call Build.cmd
if errorlevel 1 goto failed
set "PMB_WORKER=build\MotionBlurWorker.exe"
:install
if not exist "%PMB_WORKER%" goto failed
set "PMB_DEST=%APPDATA%\Adobe\CEP\extensions\com.shuttertrail.premiere"
if not exist "%PMB_DEST%\bin" mkdir "%PMB_DEST%\bin"
xcopy "panel\*" "%PMB_DEST%\" /E /I /Y /Q >nul
if errorlevel 1 goto failed
copy /Y "%PMB_WORKER%" "%PMB_DEST%\bin\MotionBlurWorker.exe" >nul
if errorlevel 1 goto failed
rem Per-user settings. Premiere 2022-2024 use CEP 11, 2025 uses CEP 12 and
rem newer releases read later keys. Setting a key that no host uses is harmless.
for %%V in (11 12 13 14 15) do (
    reg add "HKCU\Software\Adobe\CSXS.%%V" /v PlayerDebugMode /t REG_SZ /d 1 /f >nul
    if errorlevel 1 goto failed
)
echo Installed. Restart Premiere and open Window - Extensions - Motion Blur.
pause
exit /b 0
:failed
echo Build or installation failed. Read the first error above.
pause
exit /b 1
