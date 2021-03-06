solution "lasev"
    location("../build")
	configurations { "Debug", "Release" }
	
	configuration "Debug"
  	defines { "DEBUG" }
	flags {"Symbols"}

	configuration "Release"
  	defines { "NDEBUG" }
	flags {"Optimize"}

	project "lasev"
	kind "StaticLib"
	language "C"
	targetdir "../lib"

    files {"../lasev/*.h", "../lasev/*.c"}
	
	if os.is("linux") then
		excludes {"../lasev/le_iocp.c", "../lasev/le_winThreads.c", "../lasev/le_win.h"}
	elseif os.is("windows") then
		defines {"_CRT_SECURE_NO_WARNINGS"}
		excludes {"../lasev/le_epoll.c", "../lasev/le_linux.h"}
		links {"ws2_32"}
	else
		printf("don't support this platform!")
	end

