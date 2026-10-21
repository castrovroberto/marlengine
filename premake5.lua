workspace "Marle"
    architecture "x64"
    configurations 
    {
        "Debug",
        "Release",
        "Dist"
    }

outputdir="%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

-- libc++ headers from the active macOS SDK (instead of a pinned SDK version path)
local function macOSCxxIncludes()
    if os.host() ~= "macosx" then return {} end
    local sdk = os.outputof("xcrun --show-sdk-path")
    if sdk and os.isdir(sdk .. "/usr/include/c++/v1") then
        return { sdk .. "/usr/include/c++/v1" }
    end
    return {}
end

-- GLFW provides the window/context/input backend on Linux (macOS uses native Cocoa)
if os.target() == "linux" then
project "GLFW"
    location "Marle/vendor/glfw"
    kind "StaticLib"
    language "C"
    pic "On"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

    files
    {
        "Marle/vendor/glfw/include/GLFW/glfw3.h",
        "Marle/vendor/glfw/include/GLFW/glfw3native.h",
        "Marle/vendor/glfw/src/internal.h",
        "Marle/vendor/glfw/src/platform.h",
        "Marle/vendor/glfw/src/mappings.h",
        "Marle/vendor/glfw/src/context.c",
        "Marle/vendor/glfw/src/init.c",
        "Marle/vendor/glfw/src/input.c",
        "Marle/vendor/glfw/src/monitor.c",
        "Marle/vendor/glfw/src/platform.c",
        "Marle/vendor/glfw/src/vulkan.c",
        "Marle/vendor/glfw/src/window.c",
        "Marle/vendor/glfw/src/egl_context.c",
        "Marle/vendor/glfw/src/osmesa_context.c",
        "Marle/vendor/glfw/src/null_init.c",
        "Marle/vendor/glfw/src/null_monitor.c",
        "Marle/vendor/glfw/src/null_window.c",
        "Marle/vendor/glfw/src/null_joystick.c",
        "Marle/vendor/glfw/src/x11_init.c",
        "Marle/vendor/glfw/src/x11_monitor.c",
        "Marle/vendor/glfw/src/x11_window.c",
        "Marle/vendor/glfw/src/xkb_unicode.c",
        "Marle/vendor/glfw/src/glx_context.c",
        "Marle/vendor/glfw/src/posix_module.c",
        "Marle/vendor/glfw/src/posix_poll.c",
        "Marle/vendor/glfw/src/posix_thread.c",
        "Marle/vendor/glfw/src/posix_time.c",
        "Marle/vendor/glfw/src/linux_joystick.c"
    }

    defines { "_GLFW_X11" }

    filter "configurations:Debug"
        symbols "On"

    filter "configurations:Release"
        optimize "On"

    filter "configurations:Dist"
        optimize "On"
end

project "Marle"
    location "Marle"
    kind "SharedLib"
    language "C++"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

    files 
    {
        "%{prj.name}/src/**.h",
        "%{prj.name}/src/**.cpp",
        "%{prj.name}/src/**.mm",
        "%{prj.name}/vendor/glad/src/**.c"
    }

    -- pchheader "mrlpch.h"
    -- pchsource "Marle/src/mrlpch.h"

    includedirs
    {
        "%{prj.name}/src",
        "%{prj.name}/vendor/spdlog/include",
        "%{prj.name}/vendor/glad/include",
        "%{prj.name}/vendor",
        "%{prj.name}/vendor/glm"
    }

    filter "system:windows"
        cppdialect "C++17"
        staticruntime "On"
        systemversion "latest"

        removefiles { "%{prj.name}/src/**.mm", "%{prj.name}/src/Marle/Platform/macOS/**", "%{prj.name}/src/Marle/Platform/Linux/**" }

        defines 
        {
            "MRL_PLATFORM_WINDOWS"
        }

        links
        {
            "opengl32"
        }

    filter "system:macosx"
        cppdialect "C++17"
        staticruntime "On"
        buildoptions { "-stdlib=libc++" }
        linkoptions { "-stdlib=libc++" }

        removefiles { "%{prj.name}/src/Marle/Platform/Linux/**" }

        defines 
        {
            "MRL_PLATFORM_MACOS"
        }

        includedirs(macOSCxxIncludes())

        links
        {
            "OpenGL.framework",
            "Cocoa.framework",
            "Carbon.framework"
        }

    filter "system:linux"
        cppdialect "C++17"
        staticruntime "On"
        systemversion "latest"

        removefiles { "%{prj.name}/src/**.mm", "%{prj.name}/src/Marle/Platform/macOS/**" }

        defines 
        {
            "MRL_PLATFORM_LINUX"
        }

        includedirs
        {
            "%{prj.name}/vendor/glfw/include"
        }

        links
        {
            "GLFW",
            "X11",
            "dl",
            "pthread"
        }

    filter "configurations:Debug"
        symbols "On"
    
    filter "configurations:Release"
        optimize "On"

    filter "configurations:Dist"
        optimize "On"

project "Sandbox"
    location "Sandbox"
    kind "ConsoleApp"
    language "C++"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

    files 
    {
        "%{prj.name}/src/**.h",
        "%{prj.name}/src/**.cpp",
    }

    includedirs
    {
        "Marle/vendor/spdlog/include",
        "Marle/src",
        "Marle/vendor/glad/include",
        "Marle/vendor",
        "Marle/vendor/glm"
    }

    links 
    {
        "Marle"
    }

    filter "system:windows"
        cppdialect "C++17"
        staticruntime "On"
        systemversion "latest"

        defines 
        {
            "MRL_PLATFORM_WINDOWS"
        }

    filter "system:macosx"
        cppdialect "C++17"
        staticruntime "On"
        buildoptions { "-stdlib=libc++" }
        linkoptions { "-stdlib=libc++" }

        defines 
        {
            "MRL_PLATFORM_MACOS"
        }

        includedirs(macOSCxxIncludes())

    filter "system:linux"
        cppdialect "C++17"
        staticruntime "On"
        systemversion "latest"

        defines 
        {
            "MRL_PLATFORM_LINUX"
        }

        -- find libMarle.so next to the Sandbox binary's sibling output dir
        linkoptions { "-Wl,-rpath,'$$ORIGIN/../Marle'" }

    filter "configurations:Debug"
        symbols "On"
    
    filter "configurations:Release"
        optimize "On"

    filter "configurations:Dist"
        optimize "On"

-- Test executables live in Tests/<name>/src and exit non-zero on failure.
local function testProject(name)
    project (name)
        location ("Tests/" .. name)
        kind "ConsoleApp"
        language "C++"

        targetdir ("bin/" .. outputdir .. "/%{prj.name}")
        objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

        files
        {
            "Tests/" .. name .. "/src/**.h",
            "Tests/" .. name .. "/src/**.cpp"
        }

        includedirs
        {
            "Marle/vendor/spdlog/include",
            "Marle/src",
            "Marle/vendor/glad/include",
            "Marle/vendor",
            "Marle/vendor/glm"
        }

        links { "Marle" }

        filter "system:windows"
            cppdialect "C++17"
            staticruntime "On"
            systemversion "latest"
            defines { "MRL_PLATFORM_WINDOWS" }

        filter "system:macosx"
            cppdialect "C++17"
            staticruntime "On"
            buildoptions { "-stdlib=libc++" }
            linkoptions { "-stdlib=libc++" }
            defines { "MRL_PLATFORM_MACOS" }
            includedirs(macOSCxxIncludes())

        filter "system:linux"
            cppdialect "C++17"
            staticruntime "On"
            systemversion "latest"
            defines { "MRL_PLATFORM_LINUX" }
            -- find libMarle.so in the sibling output dir
            linkoptions { "-Wl,-rpath,'$$ORIGIN/../Marle'" }

        filter "configurations:Debug"
            symbols "On"

        filter "configurations:Release"
            optimize "On"

        filter "configurations:Dist"
            optimize "On"

        filter {}
end

testProject "Renderer2DTests"   -- pixel readback; needs a display, run from the repo root
testProject "InputTests"        -- headless
testProject "LogTests"          -- headless
testProject "LayerStackTests"   -- headless
testProject "ApplicationTests"  -- needs a display
