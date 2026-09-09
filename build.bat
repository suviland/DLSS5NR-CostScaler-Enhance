@echo off
setlocal
cd /d "%~dp0"

REM ---------------------------------------------------------------------------
REM Build DLSS5-NR-Boost. All final outputs go to build\<VERSION>\ and carry
REM an embedded VERSIONINFO resource (app_version.rc). Keep VERSION in sync
REM with the CHANGELOG.md and app_version.rc.
REM ---------------------------------------------------------------------------
set VERSION=0.7.0
set OUTDIR=build\%VERSION%
set OBJDIR=build\obj
if not exist "%OUTDIR%" mkdir "%OUTDIR%"
if not exist "%OBJDIR%" mkdir "%OBJDIR%"

REM Find and initialize Visual Studio environment if cl is not in PATH
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
    ) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    ) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    ) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
    )
)

REM Locate fxc.exe (DirectX Shader Compiler)
set FXC=fxc.exe
where %FXC% >nul 2>nul
if %errorlevel% neq 0 (
    if exist "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe" (
        set FXC="C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe"
    ) else if exist "C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\fxc.exe" (
        set FXC="C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\fxc.exe"
    ) else if exist "C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\fxc.exe" (
        set FXC="C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\fxc.exe"
    )
)

echo [1/8] Compiling HLSL shaders...
%FXC% /nologo /T cs_5_0 /E CS_Downsample /Fh Downsample_Shader.h /Vn g_DownsampleShader shaders.hlsl
if errorlevel 1 (
    echo [WARNING] Shader compilation failed or fxc not found, using precompiled header if present.
) else (
    %FXC% /nologo /T cs_5_0 /E CS_Resolve /Fh Resolve_Shader.h /Vn g_ResolveShader shaders.hlsl
    %FXC% /nologo /T cs_5_0 /E CS_Motion /Fh Motion_Shader.h /Vn g_MotionShader shaders.hlsl
)

echo [2/8] Compiling version resource...
rc.exe /nologo /fo app_version.res app_version.rc
if errorlevel 1 (
    echo [ERROR] Version resource compile failed.
    exit /b 1
)

echo [3/8] Compiling proxy_main.cpp...
cl.exe /nologo /O2 /Oi /GL /MT /EHsc /utf-8 /std:c++17 /D "NDEBUG" /D "_WINDOWS" /D "_USRDLL" /D "UNICODE" /D "_UNICODE" /Fo"%OBJDIR%\\" /c proxy_main.cpp
if errorlevel 1 (
    echo [ERROR] Compilation failed.
    exit /b 1
)

echo [4/8] Compiling proxy_ui.cpp...
cl.exe /nologo /O2 /Oi /GL /MT /EHsc /utf-8 /std:c++17 /D "NDEBUG" /D "_WINDOWS" /D "_USRDLL" /D "UNICODE" /D "_UNICODE" /Fo"%OBJDIR%\\" /c proxy_ui.cpp
if errorlevel 1 (
    echo [ERROR] Compilation failed.
    exit /b 1
)

echo [5/8] Linking nvngx_dlssnr.dll...
link.exe /nologo /DLL /OUT:"%OUTDIR%\nvngx_dlssnr.dll" "%OBJDIR%\proxy_main.obj" "%OBJDIR%\proxy_ui.obj" app_version.res d3d12.lib dxgi.lib kernel32.lib user32.lib gdi32.lib /OPT:REF /OPT:ICF /LTCG

if exist "%OUTDIR%\nvngx_dlssnr.dll" (
    echo [OK] %OUTDIR%\nvngx_dlssnr.dll
) else (
    echo [ERROR] Link failed.
    exit /b 1
)

echo [6/8] Building dlssnr_console.exe...
cl.exe /nologo /O2 /MT /EHsc /utf-8 /std:c++17 /D "NDEBUG" /D "UNICODE" /D "_UNICODE" /Fo"%OBJDIR%\\" dlssnr_console.cpp app_version.res /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib /OUT:"%OUTDIR%\dlssnr_console.exe"
if errorlevel 1 (
    echo [ERROR] Console EXE build failed.
    exit /b 1
)

if exist "%OUTDIR%\dlssnr_console.exe" (
    echo [OK] %OUTDIR%\dlssnr_console.exe
) else (
    echo [ERROR] Console EXE link failed.
    exit /b 1
)

echo [7/8] Compiling resources and DLSS5-NR-Boost-manager.exe...
rc.exe /nologo app.rc
if errorlevel 1 (
    echo [ERROR] Resource compile failed.
    exit /b 1
)

cl.exe /nologo /O2 /MT /EHsc /utf-8 /std:c++17 /D "NDEBUG" /D "UNICODE" /D "_UNICODE" /Fo"%OBJDIR%\\" dlssnr_manager.cpp app.res app_version.res /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib comctl32.lib version.lib ole32.lib shell32.lib gdiplus.lib /OUT:"%OUTDIR%\DLSS5NR-CostScaler-Manager.exe"
if errorlevel 1 (
    echo [ERROR] Manager EXE build failed.
    exit /b 1
)

if exist "%OUTDIR%\DLSS5NR-CostScaler-Manager.exe" (
    echo [OK] %OUTDIR%\DLSS5NR-CostScaler-Manager.exe
) else (
    echo [ERROR] Manager EXE link failed.
    exit /b 1
)

echo.
echo [SUCCESS] DLSS5-NR-Boost v%VERSION% built into %OUTDIR%\
