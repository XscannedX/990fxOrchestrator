@echo on
REM ============================================================================
REM  990fxOrchestrator build script (Windows, VS2022)
REM  ----------------------------------------------------------------------------
REM  Build entry for the EDK2 workspace. Run from the repo root.
REM  Produces:  Build\ReBarUEFI\RELEASE_VS2022\X64\990fxOrchestrator.efi
REM
REM  Prerequisites:
REM   - Visual Studio 2022 Build Tools (x64 native compiler)
REM   - NASM (set NASM_PREFIX below to your install, trailing backslash required)
REM   - EDK2 source tree, placed alongside or inside this folder
REM   - Python 3.x on PATH (for EDK2 build.py)
REM
REM  Adjust the three paths below to your local setup if they don't match.
REM ============================================================================

REM --- local adjustments: set these to your system ----------------------------
set VCVARS="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
set NASM_PREFIX=C:\NASM\
REM ----------------------------------------------------------------------------

REM Repo root = parent of this edk2\ folder
pushd "%~dp0\.."
set REPO_ROOT=%CD%
popd

set WORKSPACE=%REPO_ROOT%\edk2
set EDK_TOOLS_PATH=%REPO_ROOT%\edk2\BaseTools
set EDK_TOOLS_BIN=%REPO_ROOT%\edk2\BaseTools\Bin\Win32
set PACKAGES_PATH=%REPO_ROOT%;%REPO_ROOT%\edk2

call %VCVARS%
echo === VCVARS DONE err=%errorlevel%

cd /d %WORKSPACE%
echo CWD is now:
cd
call %WORKSPACE%\edksetup.bat
echo === EDKSETUP DONE err=%errorlevel%

call build -a X64 -b RELEASE -t VS2022 -p %REPO_ROOT%\990fxOrchestrator\990fxOrchestrator.dsc
echo === BUILD DONE err=%errorlevel%
