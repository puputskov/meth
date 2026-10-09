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

-- The parser and the evaluator recurse; Debug builds use large frames, so the
-- executables get a larger stack than the 1 MB default.
filter { "system:windows", "kind:ConsoleApp" }
	linkoptions { "/STACK:16777216" }
filter {}

-- All library code: everything under source/matlog
project "matlog_lib"
	kind "StaticLib"
	location "build"
	objdir "build/obj/%{cfg.buildcfg}/matlog_lib"
	files { "source/matlog/**.c", "include/matlog/**.h" }

	-- OS specific code lives in platform/<os>/, only the matching one is built
	filter "system:windows"
		removefiles { "source/matlog/platform/linux/**", "include/matlog/platform/linux/**" }
	filter "system:linux"
		removefiles { "source/matlog/platform/win32/**", "include/matlog/platform/win32/**" }
	filter {}

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

-- Prints decoded key events, for trying out the console layer by hand
project "keydemo"
	kind "ConsoleApp"
	location "build"
	objdir "build/obj/%{cfg.buildcfg}/keydemo"
	files { "source/demo/**.c" }
	links { "matlog_lib" }
