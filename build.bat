@echo off
setlocal
cd /d "%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" for /f "usebackq tokens=*" %%i in (`call "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
if not defined VSROOT set "VSROOT=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools"
rem VsDevCmd invokes vswhere by name. Preserve NoDefaultCurrentDirectoryInExePath.
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist bin mkdir bin
if not exist obj mkdir obj
set "FLAGS=/nologo /std:c++17 /utf-8 /W4 /EHsc /O2 /MT /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0A00 /Fo:obj\"
if /i "%~1"=="probe" (
  cl %FLAGS% src\probe.cpp /Fe:bin\probe.exe /link user32.lib ole32.lib oleaut32.lib runtimeobject.lib uuid.lib advapi32.lib dwmapi.lib
  if errorlevel 1 exit /b 1
  exit /b 0
)
if /i "%~1"=="test" (
  cl %FLAGS% tests\layout_test.cpp /Fe:bin\layout-test.exe
  if errorlevel 1 exit /b 1
  bin\layout-test.exe
  if errorlevel 1 exit /b 1
  cl %FLAGS% tests\widget_test.cpp /Fe:bin\widget-test.exe /link user32.lib
  if errorlevel 1 exit /b 1
  cl %FLAGS% tests\window_model_test.cpp /Fe:bin\window-model-test.exe /link user32.lib
  if errorlevel 1 exit /b 1
  bin\window-model-test.exe
  if errorlevel 1 exit /b 1
  cl %FLAGS% tests\multimonitor_test.cpp /Fe:bin\multimonitor-test.exe /link user32.lib ole32.lib runtimeobject.lib advapi32.lib uuid.lib
  if errorlevel 1 exit /b 1
  exit /b 0
)
rc /nologo /I src /fo obj\app.res src\app.rc
if errorlevel 1 exit /b 1
rc /nologo /I src /d PAGER_DLL /fo obj\widget.res src\app.rc
if errorlevel 1 exit /b 1
cl %FLAGS% /LD src\widget.cpp obj\widget.res /Fe:bin\vdesktop-widget.dll /link user32.lib gdi32.lib d2d1.lib dwrite.lib dcomp.lib d3d11.lib dxgi.lib comctl32.lib ole32.lib /IMPLIB:obj\vdesktop-widget.lib
if errorlevel 1 exit /b 1
cl %FLAGS% src\controller.cpp src\injector.cpp src\about.cpp obj\app.res /Fe:bin\vdesktop.exe /link /SUBSYSTEM:WINDOWS /ENTRY:wmainCRTStartup /MANIFEST:EMBED /MANIFESTINPUT:src\app.manifest user32.lib gdi32.lib dwmapi.lib ole32.lib oleaut32.lib runtimeobject.lib advapi32.lib uuid.lib d2d1.lib dwrite.lib dcomp.lib d3d11.lib dxgi.lib windowscodecs.lib comctl32.lib shell32.lib
if errorlevel 1 exit /b 1
cl %FLAGS% /DPAGER_PREVIEW src\widget.cpp obj\app.res /Fo:obj\preview.obj /Fe:bin\preview.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTINPUT:src\app.manifest user32.lib gdi32.lib d2d1.lib dwrite.lib dcomp.lib d3d11.lib dxgi.lib comctl32.lib ole32.lib
exit /b %errorlevel%
