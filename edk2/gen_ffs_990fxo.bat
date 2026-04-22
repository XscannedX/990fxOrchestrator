@echo on
REM ============================================================================
REM  990fxOrchestrator FFS generation
REM  ----------------------------------------------------------------------------
REM  Wraps the .efi built by build_990fxo.bat into an FFS file ready for
REM  MMTool insertion into a BIOS image.
REM
REM  Output:  990fxOrchestrator.ffs  (same OUTDIR as the build)
REM
REM  Run this AFTER build_990fxo.bat has produced 990fxOrchestrator.efi.
REM ============================================================================

pushd "%~dp0\.."
set REPO_ROOT=%CD%
popd

set OUTDIR=%REPO_ROOT%\edk2\Build\ReBarUEFI\RELEASE_VS2022\X64\990fxOrchestrator\990fxOrchestrator\OUTPUT
set TOOLS=%REPO_ROOT%\edk2\BaseTools\Bin\Win32
set NAME=990fxOrchestrator
set GUID=adf0508f-a992-4a0f-8b54-0291517c21aa

cd /d %OUTDIR%

REM PE32 section
%TOOLS%\GenSec.exe -S EFI_SECTION_PE32 -o %NAME%.pe32 %NAME%.efi

REM UI section (user-interface name) — keeps MMTool showing the "990fxOrchestrator" label
%TOOLS%\GenSec.exe -S EFI_SECTION_USER_INTERFACE -n %NAME% -o %NAME%.ui

REM FFS: PE32 + UI only
%TOOLS%\GenFfs.exe -t EFI_FV_FILETYPE_DRIVER -g %GUID% -o %NAME%.ffs -i %NAME%.pe32 -i %NAME%.ui

echo === GENFFS DONE err=%errorlevel%
dir %NAME%.ffs
