@echo off
rem Build + run the ZZWatch offline self-test (32-bit console exe).
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
set "OUT=%ROOT%\build\msvc-x86"
if not exist "%OUT%" mkdir "%OUT%"
if not exist "%OUT%\zzs_" mkdir "%OUT%\zzs_"
set "SRC=%ROOT%\ZolikaPatch_reconstruction\tools\watcher"
cl /nologo /std:c++17 /O2 /EHsc /W3 /arch:IA32 /DNDEBUG /DNOMINMAX /I"%SRC%" "%SRC%\selftest_zzwatch.cpp" /Fe:"%OUT%\zzwatch-selftest.exe" /Fo%OUT%\zzs_\ /link /MACHINE:X86 /SUBSYSTEM:CONSOLE /INCREMENTAL:NO /NOLOGO
if errorlevel 1 exit /b 1
"%OUT%\zzwatch-selftest.exe"
endlocal
