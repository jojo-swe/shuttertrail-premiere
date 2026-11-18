@echo off
setlocal EnableExtensions
cd /d "%~dp0"
call Build.cmd
if errorlevel 1 goto failed
set "PMB_DEST=%APPDATA%\Adobe\CEP\extensions\se.jojoswe.motionblur"
if not exist "%PMB_DEST%\bin" mkdir "%PMB_DEST%\bin"
xcopy "panel\*" "%PMB_DEST%\" /E /I /Y /Q >nul
if errorlevel 1 goto failed
copy /Y "build\MotionBlurWorker.exe" "%PMB_DEST%\bin\" >nul
if errorlevel 1 goto failed
rem Per-user settings. CEP 11 and 12 cover the target host generations.
for %%V in (11 12) do (
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
