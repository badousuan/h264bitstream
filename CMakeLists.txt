cmake_minimum_required(VERSION 3.23)

SET(CMAKE_C_STANDARD 99)

project(h264bitstream
        VERSION 0.2.0
        DESCRIPTION "A complete set of functions to read and write H.264 video bitstreams, in particular to examine or modify headers."
        HOMEPAGE_URL https://github.com/aizvorski/h264bitstream
        LANGUAGES C CXX
)

include(GNUInstallDirs)

# Manually configure wxWidgets
if(WIN32)
    set(wxWidgets_FOUND TRUE)
    set(wx_include_dir "C:/Users/7/Desktop/h264bitstream/wxWidgets/include")
    set(wx_path_debug "C:/Users/7/Desktop/h264bitstream/wxWidgets/wxMSW-3.3.2_vc14x_x64_DebugDLL/lib/vc14x_x64_dll")
    set(wx_path_release "C:/Users/7/Desktop/h264bitstream/wxWidgets/wxMSW-3.3.2_vc14x_x64_ReleaseDLL/lib/vc14x_x64_dll")

    # Build library list with explicit debug/optimized keywords
    set(wxWidgets_LIBRARIES
        # Core & Base
        optimized "${wx_path_release}/wxmsw33u_core.lib" debug "${wx_path_debug}/wxmsw33ud_core.lib"
        optimized "${wx_path_release}/wxbase33u.lib"     debug "${wx_path_debug}/wxbase33ud.lib"
        # 3rd party libs
        optimized "${wx_path_release}/wxpng.lib"         debug "${wx_path_debug}/wxpngd.lib"
        optimized "${wx_path_release}/wxzlib.lib"        debug "${wx_path_debug}/wxzlibd.lib"
        optimized "${wx_path_release}/wxjpeg.lib"        debug "${wx_path_debug}/wxjpegd.lib"
        optimized "${wx_path_release}/wxtiff.lib"        debug "${wx_path_debug}/wxtiffd.lib"
        # System libs
        comctl32.lib rpcrt4.lib shlwapi.lib version.lib ws2_32.lib
    )

    # Inclusion paths for both configs
    set(wx_inc_release "${wx_path_release}/mswu")
    set(wx_inc_debug   "${wx_path_debug}/mswud")

    add_definitions(-DwxMSVC_VERSION_AUTO -D__WXMSW__ -DwxUSE_GUI=1 -DUNICODE -D_UNICODE)
else()
    find_package(wxWidgets COMPONENTS core base)
endif()

# Adding interface target for compiler flags
add_library(compile_options INTERFACE)
if(MSVC)
    target_compile_options(compile_options INTERFACE /W4 /wd4201)
    target_compile_definitions(compile_options INTERFACE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(compile_options INTERFACE -Wall -pedantic $<IF:$<CONFIG:Debug>,-O0,-O2>)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
        target_compile_options(compile_options INTERFACE -Wextra -Wshadow -Wwrite-strings -Wno-unused -g)
    endif()
endif()
if(APPLE)
	# Compiler flags for creating universal (fat) binaries.
	#target_compile_options(compile_options INTERFACE -force_cpusubtype_ALL -mmacosx-version-min=10.4 -arch i386 -arch ppc -arch x86_64)
endif()

# Library sources
set(SOURCES
        h264_nal.c
        h264_sei.c
        h264_stream.c
        h264_analyzer.c
)

set(HEADERS
        bs.h
        h264_avcc.h
        h264_sei.h
        h264_stream.h
        h264_analyzer.h
)
# These do not participate in build -- for now...
set(ADDITIONALS
	h264_avcc.c
	h264_slice_data.c
	h264_slice_data.h
)

add_library(h264bitstream ${SOURCES} ${HEADERS})
target_sources(h264bitstream
	PUBLIC FILE_SET headers TYPE HEADERS FILES ${HEADERS}
)
target_link_libraries(h264bitstream PRIVATE compile_options)

add_executable(h264_analyze h264_analyze.c)
target_link_libraries(h264_analyze PRIVATE compile_options h264bitstream)

if(wxWidgets_FOUND)
    add_executable(h264_analyze_gui WIN32 MACOSX_BUNDLE h264_gui.cpp)
    if(WIN32)
        target_include_directories(h264_analyze_gui PRIVATE ${wx_include_dir} ${wx_inc_release} ${wx_inc_debug})
    else()
        target_include_directories(h264_analyze_gui PRIVATE ${wxWidgets_INCLUDE_DIRS})
        target_compile_definitions(h264_analyze_gui PRIVATE ${wxWidgets_DEFINITIONS})
    endif()
    # On macOS, ensure we build a bundle if needed, or just a normal executable
	if(APPLE)
    	set_target_properties(h264_analyze_gui PROPERTIES 
        	MACOSX_BUNDLE TRUE
        	MACOSX_DEPLOYMENT_TARGET "12.0"
       		MACOSX_BUNDLE_GUI_IDENTIFIER "com.virinext.h264_analyze_gui"
        	MACOSX_BUNDLE_BUNDLE_NAME "HEVCESBrowser"
        	MACOSX_BUNDLE_BUNDLE_VERSION "1.0.0"
        	MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0.0"
    	)
	endif()
    target_link_libraries(h264_analyze_gui PRIVATE compile_options h264bitstream ${wxWidgets_LIBRARIES})
    target_compile_definitions(h264_analyze_gui PRIVATE WXUSINGDLL)
endif()

add_executable(svc_split svc_split.c)
target_link_libraries(svc_split PRIVATE compile_options h264bitstream)

# Add the new dependency analyzer executable
add_executable(dep_analyzer dep_analyzer_main.c dep_analyzer.c)
target_link_libraries(dep_analyzer PRIVATE compile_options h264bitstream)

install(TARGETS h264bitstream h264_analyze svc_split dep_analyzer
	FILE_SET headers
)

if(UNIX)
	# Support for pkg-config in consuming projects
	set(prefix "${CMAKE_INSTALL_PREFIX}")
	set(exec_prefix "${CMAKE_INSTALL_PREFIX}")
	set(libdir "\${prefix}/${CMAKE_INSTALL_LIBDIR}")
	set(includedir "\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}")

	configure_file("${CMAKE_CURRENT_SOURCE_DIR}/libh264bitstream.pc.in"
		"${CMAKE_CURRENT_BINARY_DIR}/libh264bitstream.pc"
	)

	install(FILES "${CMAKE_CURRENT_BINARY_DIR}/libh264bitstream.pc"
		DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/pkgconfig"
	)
endif(UNIX)
