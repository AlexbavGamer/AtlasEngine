@echo off
rem ============================================================================
rem build_shaders.bat - compile SPIR-V shaders with glslc and embed them into
rem a generated C++ translation unit (build/generated/embedded_shaders.cpp).
rem Called from premake5 prebuild commands.
rem ============================================================================
setlocal

set "ROOT=%~dp0.."
set "GLSLC=%VULKAN_SDK%\Bin\glslc.exe"
if not exist "%GLSLC%" set "GLSLC=glslc"

set "SHADER_DIR=%ROOT%\build\shaders"
set "GEN_DIR=%ROOT%\build\generated"
mkdir "%SHADER_DIR%" 2>nul
mkdir "%GEN_DIR%" 2>nul

"%GLSLC%" -fshader-stage=vert "%ROOT%\shaders\pbr_vert.glsl"       -o "%SHADER_DIR%\pbr_vert.spv"       || goto :fail
"%GLSLC%" -fshader-stage=frag "%ROOT%\shaders\pbr_frag.glsl"       -o "%SHADER_DIR%\pbr_frag.spv"       || goto :fail
"%GLSLC%" -fshader-stage=vert "%ROOT%\shaders\pbr_instanced_vert.glsl" -o "%SHADER_DIR%\pbr_instanced_vert.spv" || goto :fail
"%GLSLC%" -fshader-stage=vert "%ROOT%\shaders\picking_vert.glsl"   -o "%SHADER_DIR%\picking_vert.spv"   || goto :fail
"%GLSLC%" -fshader-stage=frag "%ROOT%\shaders\picking_frag.glsl"   -o "%SHADER_DIR%\picking_frag.spv"   || goto :fail
"%GLSLC%" -fshader-stage=vert "%ROOT%\shaders\outline_vert.glsl"   -o "%SHADER_DIR%\outline_vert.spv"   || goto :fail
"%GLSLC%" -fshader-stage=frag "%ROOT%\shaders\outline_frag.glsl"   -o "%SHADER_DIR%\outline_frag.spv"   || goto :fail
"%GLSLC%" -fshader-stage=vert "%ROOT%\shaders\shadow_vert.glsl"    -o "%SHADER_DIR%\shadow_vert.spv"    || goto :fail
"%GLSLC%" -fshader-stage=vert "%ROOT%\shaders\ui_vert.glsl"        -o "%SHADER_DIR%\ui_vert.spv"        || goto :fail
"%GLSLC%" -fshader-stage=frag "%ROOT%\shaders\ui_frag.glsl"        -o "%SHADER_DIR%\ui_frag.spv"        || goto :fail

powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\scripts\embed_shaders.ps1" ^
    -InputDir "%SHADER_DIR%" -OutCpp "%GEN_DIR%\embedded_shaders.cpp"
if errorlevel 1 goto :fail

exit /b 0

:fail
echo [build_shaders.bat] FAILED
exit /b 1
