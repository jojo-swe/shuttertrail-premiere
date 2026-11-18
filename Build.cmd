@echo off
setlocal EnableExtensions
pushd "%~dp0" || exit /b 1
rem Discover the installed Microsoft x64 compiler and Windows SDK.
set "PMB_VS="
for /f "usebackq delims=" %%V in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "PMB_VS=%%V"
if not defined PMB_VS (
    echo Install Visual Studio Build Tools with Desktop development with C++ and a Windows SDK.
    exit /b 1
)
call "%PMB_VS%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 1
if not exist build mkdir build
pushd build || exit /b 1
set "PMB_FLAGS=/nologo /std:c++17 /O2 /EHsc /MD /utf-8 /W4 /D_WIN32_WINNT=0x0A00"
set "PMB_LIBS=mfplat.lib mfreadwrite.lib mfuuid.lib ole32.lib uuid.lib"
cl %PMB_FLAGS% ..\worker\RenderClip.cpp /Fe:MotionBlurWorker.exe /link %PMB_LIBS%
if errorlevel 1 exit /b 1
cl %PMB_FLAGS% ..\tests\MotionBlurTest.cpp /Fe:MotionBlurTest.exe
if errorlevel 1 exit /b 1
MotionBlurTest.exe || exit /b 1
cl %PMB_FLAGS% ..\tests\MediaFoundationTest.cpp /Fe:MediaFoundationTest.exe /link %PMB_LIBS%
if errorlevel 1 exit /b 1
set "PMB_FIXTURE=%TEMP%\motion-blur-%RANDOM%-%RANDOM%"
mkdir "%PMB_FIXTURE%" || exit /b 1
MediaFoundationTest.exe "%PMB_FIXTURE%"
if errorlevel 1 (
    echo Test files are available at "%PMB_FIXTURE%"
    exit /b 1
)
del "%PMB_FIXTURE%\source.mp4" "%PMB_FIXTURE%\trim.mp4" "%PMB_FIXTURE%\index-source.mp4" "%PMB_FIXTURE%\index-trim.mp4" "%PMB_FIXTURE%\offset-source.mp4" "%PMB_FIXTURE%\offset-trim.mp4"
rmdir "%PMB_FIXTURE%"
popd
popd
exit /b 0
