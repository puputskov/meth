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

	language "C"
	cdialect "C11"
	warnings "Extra"
	staticruntime "Off"
	defines { "_CRT_SECURE_NO_WARNINGS" }
	includedirs { "include" }
	targetdir "build/bin/%{cfg.buildcfg}"

-- All library code: everything under source/matlog
project "matlog_lib"
	kind "StaticLib"
	location "build"
	objdir "build/obj/%{cfg.buildcfg}/matlog_lib"
	files { "source/matlog/**.c", "include/matlog/**.h" }

-- The interactive shell
project "matlog"
	kind "ConsoleApp"
	location "build"
	objdir "build/obj/%{cfg.buildcfg}/matlog"
	files { "source/app/**.c" }
	links { "matlog_lib" }

-- Unit tests, run with build/bin/<config>/tests.exe
project "tests"
	kind "ConsoleApp"
	location "build"
	objdir "build/obj/%{cfg.buildcfg}/tests"
	files { "source/tests/**.c", "source/tests/**.h" }
	links { "matlog_lib" }
