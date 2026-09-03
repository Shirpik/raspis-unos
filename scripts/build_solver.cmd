@echo off
setlocal
set "VSWHERE=C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo Visual Studio Installer ^(vswhere.exe^) not found.
  exit /b 2
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
if not defined VSROOT (
  echo Visual Studio with C++ tools not found.
  exit /b 2
)
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
set "CMAKE=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE%" set "CMAKE=cmake"
if not defined ORTOOLS_ROOT set "ORTOOLS_ROOT=C:\or-tools"
if not exist "build\CMakeCache.txt" (
  "%CMAKE%" -S . -B build -DCMAKE_PREFIX_PATH="%ORTOOLS_ROOT%" -DBUILD_TESTING=ON
  if errorlevel 1 exit /b %errorlevel%
)
"%CMAKE%" --build build --config Release --target timetable_solver quota_optimizer scheduler_rules_test schedule_validator_test room_policy_test
