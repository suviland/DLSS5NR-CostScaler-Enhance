@echo off
setlocal
cd /d "%~dp0"

where cl.exe >nul 2>nul
if %errorlevel% neq 0 (
    if exist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
    ) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
    )
)

echo Compiling companion_main.cpp...
cl.exe /nologo /O2 /Oi /GL /MT /EHsc /std:c++17 /D "NDEBUG" /D "_WINDOWS" /D "_USRDLL" /I include /c companion_main.cpp
if errorlevel 1 (
    echo [ERROR] Compilation failed.
    exit /b 1
)

echo Linking dlssnr-companion.addon64...
link.exe /nologo /DLL /OUT:dlssnr-companion.addon64 companion_main.obj kernel32.lib user32.lib /OPT:REF /OPT:ICF /LTCG

if exist dlssnr-companion.addon64 (
    echo [SUCCESS] Built dlssnr-companion.addon64
) else (
    echo [ERROR] Link failed.
    exit /b 1
)
