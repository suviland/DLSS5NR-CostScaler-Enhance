@echo off
setlocal
cd /d "%~dp0"

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

echo Compiling HLSL shaders...
%FXC% /nologo /T cs_5_0 /E CS_Downsample /Fh Downsample_Shader.h /Vn g_DownsampleShader shaders.hlsl
if errorlevel 1 (
    echo [WARNING] Shader compilation failed or fxc not found, using precompiled header if present.
) else (
    %FXC% /nologo /T cs_5_0 /E CS_Resolve /Fh Resolve_Shader.h /Vn g_ResolveShader shaders.hlsl
)

echo Compiling proxy_main.cpp...
cl.exe /nologo /O2 /Oi /GL /MT /EHsc /utf-8 /std:c++17 /D "NDEBUG" /D "_WINDOWS" /D "_USRDLL" /D "UNICODE" /D "_UNICODE" /c proxy_main.cpp
if errorlevel 1 (
    echo [ERROR] Compilation failed.
    exit /b 1
)

echo Compiling proxy_ui.cpp...
cl.exe /nologo /O2 /Oi /GL /MT /EHsc /utf-8 /std:c++17 /D "NDEBUG" /D "_WINDOWS" /D "_USRDLL" /D "UNICODE" /D "_UNICODE" /c proxy_ui.cpp
if errorlevel 1 (
    echo [ERROR] Compilation failed.
    exit /b 1
)

echo Linking nvngx_dlssnr.dll...
link.exe /nologo /DLL /OUT:nvngx_dlssnr.dll proxy_main.obj proxy_ui.obj d3d12.lib dxgi.lib kernel32.lib user32.lib gdi32.lib /OPT:REF /OPT:ICF /LTCG

if exist nvngx_dlssnr.dll (
    echo [SUCCESS] Built nvngx_dlssnr.dll
) else (
    echo [ERROR] Link failed.
    exit /b 1
)

echo Compiling dlssnr_console.exe...
cl.exe /nologo /O2 /MT /EHsc /utf-8 /std:c++17 /D "NDEBUG" /D "UNICODE" /D "_UNICODE" dlssnr_console.cpp /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib /OUT:dlssnr_console.exe
if errorlevel 1 (
    echo [ERROR] Console EXE build failed.
    exit /b 1
)

if exist dlssnr_console.exe (
    echo [SUCCESS] Built dlssnr_console.exe
) else (
    echo [ERROR] Console EXE link failed.
    exit /b 1
)
