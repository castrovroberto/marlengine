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
    }

    includedirs
    {
        "%{prj.name}/vendor/spdlog/include"
    }

    filter "system:Unix"
        system "linux"
        cppdialect "Default"
        staticruntime "On"
        systemversion "latest"

        defines 
        {
            MRL_PLATFORM_LINUX
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

    filter "system:Unix"
        system "linux"
        cppdialect "Default"
        staticruntime "On"
        systemversion "latest"

        defines 
        {
            MRL_PLATFORM_LINUX
        }


    filter "configurations:Debug"
        symbols "On"
    
    filter "configurations:Release"
        optimize "On"

    filter "configurations:Dist"
        optimize "On"