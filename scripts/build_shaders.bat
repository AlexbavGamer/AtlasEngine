@echo off
rem ============================================================================
rem build_shaders.bat - compile SPIR-V shaders with glslc and embed them into
rem a generated C++ translation unit (build/generated/embedded_shaders.cpp).
rem Called from premake5 prebuild commands.
rem Auto-discovers all *_vert.glsl and *_frag.glsl files in shaders/ directory.
rem ============================================================================
setlocal enabledelayedexpansion

set "ROOT=%~dp0.."
set "GLSLC=%VULKAN_SDK%\Bin\glslc.exe"
if not exist "%GLSLC%" set "GLSLC=glslc"

set "SHADER_DIR=%ROOT%\build\shaders"
set "GEN_DIR=%ROOT%\build\generated"
mkdir "%SHADER_DIR%" 2>nul
mkdir "%GEN_DIR%" 2>nul

rem Auto-discover vertex shaders (*_vert.glsl) and fragment shaders (*_frag.glsl)
set "SHADER_NAMES="
for %%f in ("%ROOT%\shaders\*_vert.glsl") do (
    if exist "%%f" (
        set "FILENAME=%%~nf"
        rem FILENAME will be like "pbr_vert" (without .glsl)
        set "BASENAME=!FILENAME:_vert=!"
        set "SHADER_NAMES=!SHADER_NAMES! !BASENAME!"
        echo [build_shaders.bat] Found vertex shader: !BASENAME!
    )
)

for %%f in ("%ROOT%\shaders\*_frag.glsl") do (
    if exist "%%f" (
        set "FILENAME=%%~nf"
        rem FILENAME will be like "pbr_frag" (without .glsl)
        set "BASENAME=!FILENAME:_frag=!"
        set "SHADER_NAMES=!SHADER_NAMES! !BASENAME!"
        echo [build_shaders.bat] Found fragment shader: !BASENAME!
    )
)

rem Compile vertex shaders
for %%n in (!SHADER_NAMES!) do (
    if exist "%ROOT%\shaders\%%n_vert.glsl" (
        "%GLSLC%" -fshader-stage=vert "%ROOT%\shaders\%%n_vert.glsl" -o "%SHADER_DIR%\%%n.spv" || goto :fail
    )
)

rem Compile fragment shaders
for %%n in (!SHADER_NAMES!) do (
    if exist "%ROOT%\shaders\%%n_frag.glsl" (
        "%GLSLC%" -fshader-stage=frag "%ROOT%\shaders\%%n_frag.glsl" -o "%SHADER_DIR%\%%n.spv" || goto :fail
    )
)

rem Embed shaders (auto-discovers .spv files in SHADER_DIR)
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\scripts\embed_shaders.ps1" ^
    -InputDir "%SHADER_DIR%" -OutCpp "%GEN_DIR%\embedded_shaders.cpp"
if errorlevel 1 goto :fail

exit /b 0

:fail
echo [build_shaders.bat] FAILED
exit /b 1