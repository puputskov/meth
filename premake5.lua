workspace "matlog"
	location "build"
	configurations { "Debug", "Release" }
	platforms { "x64" }
	architecture "x86_64"
	startproject "matlog"
	debugdir ""

	filter "configurations:Debug"
		defines { "DEBUG" }
		symbols "On"
		-- Edit and Continue (/ZI) triples the size of every stack frame
		editandcontinue "Off"
		optimize "Off"

	filter "configurations:Release"
		defines { "NDEBUG" }
		symbols "Off"
		optimize "Speed"
	filter {}

	language "C"
	cdialect "C11"
	warnings "Extra"
	-- a warning is an error (/WX, and -Werror in the Makefile)
	fatalwarnings { "All" }
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

	-- OS specific code lives in platform/<os>/, only the matching one is built.
	-- Linux and macOS share platform/posix/ (termios, poll and POSIX files).
	filter "system:windows"
		removefiles { "source/matlog/platform/posix/**", "include/matlog/platform/posix/**" }
	filter "system:not windows"
		removefiles { "source/matlog/platform/win32/**", "include/matlog/platform/win32/**" }
	filter {}

-- The interactive shell
project "matlog"
	kind "ConsoleApp"
	location "build"
	objdir "build/obj/%{cfg.buildcfg}/matlog"
	files { "source/app/**.c" }
	links { "matlog_lib" }
	filter "system:not windows"
		links { "m" } -- the math functions are in libm outside of Windows
	filter {}

-- Unit tests, run with build/bin/<config>/tests.exe
project "tests"
	kind "ConsoleApp"
	location "build"
	objdir "build/obj/%{cfg.buildcfg}/tests"
	files { "source/tests/**.c", "source/tests/**.h" }
	links { "matlog_lib" }
	filter "system:not windows"
		links { "m" } -- the math functions are in libm outside of Windows
	filter {}

-- Prints decoded key events, for trying out the console layer by hand
project "keydemo"
	kind "ConsoleApp"
	location "build"
	objdir "build/obj/%{cfg.buildcfg}/keydemo"
	files { "source/demo/**.c" }
	links { "matlog_lib" }
	filter "system:not windows"
		links { "m" } -- the math functions are in libm outside of Windows
	filter {}
