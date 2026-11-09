@echo off
rem Build the ZZWatch.asi decision-stream observer (32-bit, MSVC).
rem Usage: build_zzwatch.cmd [vcvars32.bat] [outdir]
setlocal EnableExtensions
set "VCVARS=%~1"
if "%VCVARS%"=="" set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat"
if not exist "%VCVARS%" (
  echo error: vcvars32.bat not found at "%VCVARS%" 1>&2
  exit /b 2
)
call "%VCVARS%" >nul || (echo error: vcvars32 failed 1>&2 & exit /b 2)
set "ROOT=%~dp0..\..\.."
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
set "OUT=%~2"
if "%OUT%"=="" set "OUT=%ROOT%\build\msvc-x86"
if not exist "%OUT%" mkdir "%OUT%"
if not exist "%OUT%\zz_" mkdir "%OUT%\zz_"
cl /nologo /std:c++17 /O2 /EHsc /W3 /DNDEBUG /DNOMINMAX /arch:IA32 "%ROOT%\ZolikaPatch_reconstruction\tools\watcher\zzwatch.cpp" /LD /Fe:"%OUT%\ZZWatch.asi" /Fo%OUT%\zz_\ /link /MACHINE:X86 /SUBSYSTEM:WINDOWS /INCREMENTAL:NO /NOLOGO
if errorlevel 1 exit /b 1
echo built "%OUT%\ZZWatch.asi"
endlocal
