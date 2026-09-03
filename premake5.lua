-- premake5.lua — AtlasEngine build script
-- Run: premake5 ninja   (or: premake5 vs2022)

-- ---------------------------------------------------------------------------
-- Build SPIR-V shaders + embedded_shaders.cpp during configuration so the
-- generated .cpp exists when the project is generated. Re-run premake5 after
-- editing shaders (avoids cmd /C quote-escaping issues in the ninja action).
-- ---------------------------------------------------------------------------
if os.host() == "windows" then
    local script = path.join(os.getcwd(), "scripts/build_shaders.bat")
    local ok = os.execute('call "' .. script .. '"')
    if ok == nil or ok == false or (type(ok) == "number" and ok ~= 0) then
        error("premake5: failed to build shaders (" .. script .. ")")
    end
end

workspace "AtlasEngine"
    location "build"
    configurations { "Debug", "Release" }
    architecture "x86_64"
    startproject "AtlasEngine"

    -- Use the MinGW/GCC toolchain (available on PATH) with the Ninja action.
    toolset "gcc"

-- ---------------------------------------------------------------------------
-- Vulkan SDK detection
-- ---------------------------------------------------------------------------
vulkan_sdk = os.getenv("VULKAN_SDK")
if vulkan_sdk == nil or vulkan_sdk == "" then
    vulkan_sdk = "C:/VulkanSDK/1.4.357.0"
end
vulkan_inc = path.join(vulkan_sdk, "Include")
vulkan_lib = path.join(vulkan_sdk, "Lib")

-- ---------------------------------------------------------------------------
-- Paths
-- ---------------------------------------------------------------------------
deps_src = path.getabsolute("deps/src")
deps_build = path.getabsolute("deps/build")

-- ---------------------------------------------------------------------------
-- Compiler flags
-- ---------------------------------------------------------------------------
filter "configurations:Debug"
    defines { "DEBUG" }
    symbols "On"

filter "configurations:Release"
    defines { "NDEBUG" }
    optimize "Speed"

-- ---------------------------------------------------------------------------
-- Dependencies (header-only) — just include dirs, referenced by AtlasEngine
-- ---------------------------------------------------------------------------
-- GLM, STB, EnTT, VMA are header-only; no project needed.

-- ---------------------------------------------------------------------------
-- GLFW
-- ---------------------------------------------------------------------------
project "glfw"
    kind "StaticLib"
    language "C"
    cdialect "C99"
    warnings "Extra"

    files { path.join(deps_src, "glfw/src/*.c") }

    includedirs {
        path.join(deps_src, "glfw/include"),
        path.join(deps_src, "glfw/src"),
    }

    defines {
        "_GLFW_WIN32",
        "UNICODE",
        "_UNICODE",
        "WINVER=0x0501",
    }

    filter "system:windows"
        links { "user32", "gdi32", "shell32" }

    filter {}

-- ---------------------------------------------------------------------------
-- ImGui + ImGuiFileDialog
-- ---------------------------------------------------------------------------
project "imgui_lib"
    kind "StaticLib"
    language "C++"
    cppdialect "C++17"

    files {
        path.join(deps_src, "imgui/imgui.cpp"),
        path.join(deps_src, "imgui/imgui_draw.cpp"),
        path.join(deps_src, "imgui/imgui_tables.cpp"),
        path.join(deps_src, "imgui/imgui_widgets.cpp"),
        path.join(deps_src, "imgui/imgui_demo.cpp"),
        path.join(deps_src, "imgui/backends/imgui_impl_glfw.cpp"),
        path.join(deps_src, "imgui/backends/imgui_impl_vulkan.cpp"),
        path.join(deps_src, "imguifiledialog/ImGuiFileDialog.cpp"),
    }

    includedirs {
        path.join(deps_src, "imgui"),
        path.join(deps_src, "imgui/backends"),
        path.join(deps_src, "imguifiledialog"),
        path.join(deps_src, "imguizmo/src"),
        path.join(deps_src, "glfw/include"),
        vulkan_inc,
    }

    defines { "IMGUI_DEFINE_MATH_OPERATORS" }

    links { "glfw", "vulkan-1" }

-- ---------------------------------------------------------------------------
-- ImGuizmo
-- ---------------------------------------------------------------------------
project "imguizmo_lib"
    kind "StaticLib"
    language "C++"
    cppdialect "C++17"

    files { path.join(deps_src, "imguizmo/src/ImGuizmo.cpp") }
    includedirs {
        path.join(deps_src, "imguizmo/src"),
        path.join(deps_src, "imgui"),
    }
    defines { "IMGUI_DEFINE_MATH_OPERATORS" }

-- ---------------------------------------------------------------------------
-- Lua
-- ---------------------------------------------------------------------------
project "lua_lib"
    kind "StaticLib"
    language "C"
    cdialect "C99"

    files { path.join(deps_src, "lua/*.c") }
    removefiles {
        path.join(deps_src, "lua/lua.c"),
        path.join(deps_src, "lua/luac.c"),
        path.join(deps_src, "lua/onelua.c"),
    }
    includedirs { path.join(deps_src, "lua") }

    if os.istarget("windows") then
        defines { "_CRT_SECURE_NO_WARNINGS" }
    end

-- ---------------------------------------------------------------------------
-- Tracy profiler client
-- ---------------------------------------------------------------------------
project "TracyClient"
    kind "StaticLib"
    language "C++"
    cppdialect "C++17"

    files { path.join(deps_src, "tracy/public/TracyClient.cpp") }

    includedirs { path.join(deps_src, "tracy/public") }

    defines {
        "TRACY_ENABLE",
        "TRACY_NO_FRAME_IMAGE",
    }

-- ---------------------------------------------------------------------------
-- Zlib (for assimp)
-- ---------------------------------------------------------------------------
project "zlib"
    kind "StaticLib"
    language "C"

    files { path.join(deps_src, "assimp/contrib/zlib/*.c") }

    includedirs {
        path.join(deps_src, "assimp/contrib/zlib"),
        path.join(deps_build, "assimp/contrib/zlib"),  -- for zconf.h (generated by CMake)
    }

-- ---------------------------------------------------------------------------
-- Assimp
-- ---------------------------------------------------------------------------
project "assimp"
    kind "StaticLib"
    language "C++"
    cppdialect "C++17"

    -- Source files: all of code/ + a few contrib libraries
    files {
        path.join(deps_src, "assimp/code/**.cpp"),
        -- Contrib files compiled as part of assimp
        path.join(deps_src, "assimp/contrib/Open3DGC/*.cpp"),
        path.join(deps_src, "assimp/contrib/clipper/clipper.cpp"),
        path.join(deps_src, "assimp/contrib/openddlparser/code/*.cpp"),
        path.join(deps_src, "assimp/contrib/poly2tri/**/*.cc"),
        path.join(deps_src, "assimp/contrib/pugixml/src/pugixml.cpp"),
        path.join(deps_src, "assimp/contrib/unzip/*.c"),
        path.join(deps_src, "assimp/contrib/zip/src/*.c"),
    }

    -- Zlib sources (separate project for cleanliness, but we need to compile
    -- them as part of assimp; zlib project handles that, link it)
    -- Actually we compile zlib as a separate static lib and link it here.
    links { "zlib" }

    -- Include directories
    includedirs {
        path.join(deps_src, "assimp/include"),          -- config.h, assimp headers
        path.join(deps_src, "assimp/code"),
        path.join(deps_src, "assimp"),
        path.join(deps_src, "assimp/contrib/zlib"),
        path.join(deps_build, "assimp/contrib/zlib"),    -- zconf.h generated
        path.join(deps_src, "assimp/contrib/pugixml/src"),
        path.join(deps_src, "assimp/contrib/utf8cpp/source"),
        path.join(deps_src, "assimp/contrib/rapidjson/include"),
        path.join(deps_src, "assimp/contrib"),
        path.join(deps_src, "assimp/contrib/unzip"),
        path.join(deps_src, "assimp/contrib/openddlparser/include"),
    }

    -- Defines: importers/exporters disabled (from the CMake build)
    defines {
        "ASSIMP_BUILD_NO_3DS_EXPORTER",
        "ASSIMP_BUILD_NO_3DS_IMPORTER",
        "ASSIMP_BUILD_NO_AC_IMPORTER",
        "ASSIMP_BUILD_NO_AMF_IMPORTER",
        "ASSIMP_BUILD_NO_ASE_IMPORTER",
        "ASSIMP_BUILD_NO_ASSBIN_EXPORTER",
        "ASSIMP_BUILD_NO_ASSBIN_IMPORTER",
        "ASSIMP_BUILD_NO_ASSXML_EXPORTER",
        "ASSIMP_BUILD_NO_B3D_IMPORTER",
        "ASSIMP_BUILD_NO_BLEND_IMPORTER",
        "ASSIMP_BUILD_NO_BVH_IMPORTER",
        "ASSIMP_BUILD_NO_C4D_IMPORTER",
        "ASSIMP_BUILD_NO_COB_IMPORTER",
        "ASSIMP_BUILD_NO_CSM_IMPORTER",
        "ASSIMP_BUILD_NO_DXF_IMPORTER",
        "ASSIMP_BUILD_NO_GLTF_EXPORTER",
        "ASSIMP_BUILD_NO_HMP_IMPORTER",
        "ASSIMP_BUILD_NO_IFC_IMPORTER",
        "ASSIMP_BUILD_NO_IQM_IMPORTER",
        "ASSIMP_BUILD_NO_IRR_IMPORTER",
        "ASSIMP_BUILD_NO_LWO_IMPORTER",
        "ASSIMP_BUILD_NO_LWS_IMPORTER",
        "ASSIMP_BUILD_NO_M3D_EXPORTER",
        "ASSIMP_BUILD_NO_M3D_IMPORTER",
        "ASSIMP_BUILD_NO_MD2_IMPORTER",
        "ASSIMP_BUILD_NO_MD3_IMPORTER",
        "ASSIMP_BUILD_NO_MD5_IMPORTER",
        "ASSIMP_BUILD_NO_MDC_IMPORTER",
        "ASSIMP_BUILD_NO_MDL_IMPORTER",
        "ASSIMP_BUILD_NO_MMD_IMPORTER",
        "ASSIMP_BUILD_NO_MS3D_IMPORTER",
        "ASSIMP_BUILD_NO_NDO_IMPORTER",
        "ASSIMP_BUILD_NO_NFF_IMPORTER",
        "ASSIMP_BUILD_NO_OGRE_IMPORTER",
        "ASSIMP_BUILD_NO_OPENGEX_EXPORTER",
        "ASSIMP_BUILD_NO_OPENGEX_IMPORTER",
        "ASSIMP_BUILD_NO_PBRT_EXPORTER",
        "ASSIMP_BUILD_NO_PLY_EXPORTER",
        "ASSIMP_BUILD_NO_PLY_IMPORTER",
        "ASSIMP_BUILD_NO_Q3BSP_IMPORTER",
        "ASSIMP_BUILD_NO_Q3D_IMPORTER",
        "ASSIMP_BUILD_NO_RAW_IMPORTER",
        "ASSIMP_BUILD_NO_SIB_IMPORTER",
        "ASSIMP_BUILD_NO_SMD_IMPORTER",
        "ASSIMP_BUILD_NO_STEP_EXPORTER",
        "ASSIMP_BUILD_NO_STL_EXPORTER",
        "ASSIMP_BUILD_NO_STL_IMPORTER",
        "ASSIMP_BUILD_NO_TERRAGEN_IMPORTER",
        "ASSIMP_BUILD_NO_USD_IMPORTER",
        "ASSIMP_BUILD_NO_VRML_IMPORTER",
        "ASSIMP_BUILD_NO_X3D_EXPORTER",
        "ASSIMP_BUILD_NO_X3D_IMPORTER",
        "ASSIMP_BUILD_NO_X_EXPORTER",
        "ASSIMP_BUILD_NO_X_IMPORTER",
        "ASSIMP_IMPORTER_GLTF_USE_OPEN3DGC=1",
        "MINIZ_USE_UNALIGNED_LOADS_AND_STORES=0",
        "OPENDDLPARSER_BUILD",
        "OPENDDL_STATIC_LIBARY",
        "P2T_STATIC_EXPORTS",
        "RAPIDJSON_HAS_STDSTRING=1",
        "RAPIDJSON_NOMEMBERITERATORCLASS",
        "WIN32_LEAN_AND_MEAN",
    }

    -- Compiler flags from the CMake build
    -- -Wa,-mbig-obj is needed on MinGW for large object files (assimp has
    -- many symbols).  -fvisibility=hidden and -fno-strict-aliasing are also
    -- standard assimp flags.
    buildoptions {
        "-Wa,-mbig-obj",
        "-fvisibility=hidden",
        "-fno-strict-aliasing",
        "-Wno-dangling-reference",
        "-Wall",
        "-Wno-long-long",
    }

-- ---------------------------------------------------------------------------
-- Atlas UI — custom UI library (retained-mode widgets) as a static lib
-- ---------------------------------------------------------------------------
project "atlas_ui_lib"
    kind "StaticLib"
    language "C++"
    cppdialect "C++17"

    files {
        "libs/atlas_ui/**.cpp",
        "libs/atlas_ui/**.h",
    }

    includedirs {
        "libs/atlas_ui/include",
        path.join(deps_src, "glm"),
        path.join(deps_src, "stb"),
        path.join(deps_src, "vma/include"),
        vulkan_inc,
    }

    links { "vulkan-1" }

-- ---------------------------------------------------------------------------
-- AtlasEngine — the final game executable
-- ---------------------------------------------------------------------------
project "AtlasEngine"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++17"

    targetdir "bin/%{cfg.buildcfg}"
    debugdir "bin/%{cfg.buildcfg}"

    -- All source files
    files {
        "src/**.cpp",
        "src/**.h",
        "build/generated/embedded_shaders.cpp",
    }

    -- The game main is a separate entry point (AtlasGame project).
    removefiles { "src/game_main.cpp" }

    includedirs {
        "src",
        path.join(deps_src, "imgui"),
        path.join(deps_src, "imgui/backends"),
        path.join(deps_src, "imguifiledialog"),
        path.join(deps_src, "imguizmo/src"),
        path.join(deps_src, "glm"),
        path.join(deps_src, "stb"),
        path.join(deps_src, "entt/src"),
        path.join(deps_src, "vma/include"),
        path.join(deps_src, "assimp/include"),
        path.join(deps_src, "lua"),
        path.join(deps_src, "glfw/include"),
        path.join(deps_src, "tracy/public"),
        vulkan_inc,
    }

    links {
        "glfw",
        "imgui_lib",
        "imguizmo_lib",
        "lua_lib",
        "assimp",
        "zlib",
        "TracyClient",
        "vulkan-1",
        "atlas_ui_lib",
    }

    libdirs { vulkan_lib }

    defines {
        "VMA_STATIC",
        "TRACY_ENABLE",
        "TRACY_NO_FRAME_IMAGE",
        "ATLAS_EMBED_SHADERS=1",
    }

    filter "system:windows"
        links { "ole32", "shell32", "shlwapi", "uuid", "ws2_32", "dbghelp", "gdi32" }

    filter {}

    -- Prebuild: compile shaders + generate embedded_shaders.cpp
    -- Removed: premake5's ninja action has quote-escaping issues with cmd /C.
    -- Instead, shaders are built during premake5 configuration (see top of file).
    -- prebuildcommands { ... }

    -- Postbuild: copy compiled shaders next to the executable
    -- (Disabled: shaders are embedded via ATLAS_EMBED_SHADERS)
    -- postbuildcommands { ... }