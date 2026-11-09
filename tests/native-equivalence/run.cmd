@echo off
rem Builds rug_native.dll from this checkout twice, with and without the sleep threshold, and compares both
rem with a reference build of the original code.
rem Usage: run.cmd <reference rug_native.dll>
setlocal
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS=%%i"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "HERE=%~dp0"
set "ROOT=%~dp0..\.."
set "OUT=%LOCALAPPDATA%\rug-equivalence"
if not exist "%OUT%" mkdir "%OUT%"
copy /y "%~1" "%OUT%\rug_reference.dll" >nul || exit /b 1

set CL=
msbuild "%ROOT%\rug_native\rug_native.vcxproj" /nologo /v:quiet /p:Configuration=Release /p:Platform=x64 "/p:OutDir=%OUT%\sleep\\" "/p:IntDir=%OUT%\sleep-obj\\" || exit /b 1
set CL=/DRUG_EXACT_STEPS
msbuild "%ROOT%\rug_native\rug_native.vcxproj" /nologo /v:quiet /p:Configuration=Release /p:Platform=x64 "/p:OutDir=%OUT%\exact\\" "/p:IntDir=%OUT%\exact-obj\\" || exit /b 1
set CL=
copy /y "%OUT%\sleep\rug_native.dll" "%OUT%\rug_candidate_sleep.dll" >nul || exit /b 1
copy /y "%OUT%\exact\rug_native.dll" "%OUT%\rug_candidate_exact.dll" >nul || exit /b 1

cl /nologo /O2 /EHsc /std:c++20 /Fo"%OUT%\\" /Fe"%OUT%\equivalence.exe" "%HERE%equivalence.cpp" || exit /b 1
set "RUG1=%ROOT%\Rug\Resources\rugs\1 persian.jpg"
set "RUG5=%ROOT%\Rug\Resources\rugs\5 turkish.jpg"

echo == exact: every frame bit-identical with the reference
"%OUT%\equivalence.exe" exact "%OUT%\rug_reference.dll" "%OUT%\rug_candidate_exact.dll" "%RUG1%" "%RUG5%" || exit /b 1
echo == sleep: a cloth resting on icon bumps
"%OUT%\equivalence.exe" sleep "%OUT%\rug_reference.dll" "%OUT%\rug_candidate_sleep.dll" "%RUG1%" "%RUG5%" || exit /b 1
