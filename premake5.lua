workspace "Marle"
    architecture "x64"
    configurations 
    {
        "Debug",
        "Release",
        "Dist"
    }

outputdir="%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

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
        "%{prj.name}/vendor/glad/include"
    }

    filter "system:windows"
        cppdialect "C++17"
        staticruntime "On"
        systemversion "latest"

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

        defines 
        {
            "MRL_PLATFORM_MACOS"
        }

        includedirs
        {
            "/Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk/usr/include/c++/v1"
        }

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

        defines 
        {
            "MRL_PLATFORM_LINUX"
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
        "Marle/src"
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

        includedirs
        {
            "/Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk/usr/include/c++/v1"
        }

    filter "system:linux"
        cppdialect "C++17"
        staticruntime "On"
        systemversion "latest"

        defines 
        {
            "MRL_PLATFORM_LINUX"
        }

    filter "configurations:Debug"
        symbols "On"
    
    filter "configurations:Release"
        optimize "On"

    filter "configurations:Dist"
        optimize "On"