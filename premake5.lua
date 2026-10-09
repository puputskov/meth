workspace "matlog"
	location "build"
	configurations { "Debug", "Release" }
	platforms { "x64" }
	architecture "x86_64"
	startproject "matlog"

	filter "configurations:Debug"
		defines { "DEBUG" }
		symbols "On"
		optimize "Off"

	filter "configurations:Release"
		defines { "NDEBUG" }
		symbols "Off"
		optimize "Speed"
	filter {}

project "matlog"
	kind "ConsoleApp"
	language "C"
	cdialect "C11"
	location "build"

	targetdir "build/bin/%{cfg.buildcfg}"
	objdir "build/obj/%{cfg.buildcfg}"

	files { "source/**.c", "include/**.h" }
	includedirs { "include" }

	warnings "Extra"
	staticruntime "Off"
	defines { "_CRT_SECURE_NO_WARNINGS" }
