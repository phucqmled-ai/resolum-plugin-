cmake_minimum_required(VERSION 3.20)

# macOS: build a universal binary (Apple Silicon + Intel) so it loads in native-ARM Resolume.
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "")

project(QMResolumePlugins VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Resolume FFGL SDK (downloaded only, not built through its own CMake)
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(ffgl
    GIT_REPOSITORY https://github.com/resolume/ffgl.git
    GIT_TAG        5b6e07840277df9ec35a228b9eb067636316350f
    SOURCE_SUBDIR  _do_not_build
)
FetchContent_MakeAvailable(ffgl)
set(FFGL_LIB ${ffgl_SOURCE_DIR}/source/lib)

# ---------------------------------------------------------------------------
# Each plugin is a loadable module (.dll on Windows, .bundle on macOS)
# ---------------------------------------------------------------------------
function(add_ffgl_plugin NAME BUNDLE_ID)
    add_library(${NAME} MODULE
        source/${NAME}.h
        source/${NAME}.cpp
        ${FFGL_LIB}/FFGLSDK.cpp
    )
    target_include_directories(${NAME} PRIVATE ${FFGL_LIB})

    if(WIN32)
        # Static GLEW shipped inside the SDK, so the .dll has no extra dependencies.
        set(GLEW_DIR ${ffgl_SOURCE_DIR}/deps/glew-2.1.0)
        target_include_directories(${NAME} PRIVATE ${GLEW_DIR}/include)
        target_compile_definitions(${NAME} PRIVATE GLEW_STATIC NOMINMAX _CRT_SECURE_NO_WARNINGS)
        target_link_libraries(${NAME} PRIVATE ${GLEW_DIR}/lib/Release/x64/glew32s.lib opengl32)
        # Same C runtime as Resolume's own example projects (/MD). Resolume installs it.
        set_property(TARGET ${NAME} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    elseif(APPLE)
        set_target_properties(${NAME} PROPERTIES
            BUNDLE TRUE
            BUNDLE_EXTENSION bundle
            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in
            MACOSX_BUNDLE_GUI_IDENTIFIER ${BUNDLE_ID}
            MACOSX_BUNDLE_BUNDLE_NAME ${NAME}
        )
        target_compile_definitions(${NAME} PRIVATE TARGET_OS_MAC=1)
        target_link_libraries(${NAME} PRIVATE
            "-framework OpenGL" "-framework Carbon" "-framework AppKit")
    else()
        # Linux is not a Resolume platform; this branch only exists for compile checks.
        target_include_directories(${NAME} PRIVATE ${ffgl_SOURCE_DIR}/deps/glew-2.1.0/include)
        target_compile_definitions(${NAME} PRIVATE GLEW_NO_GLU)
    endif()

    # Put the result straight into  build/out/  for easy copying.
    set_target_properties(${NAME} PROPERTIES
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out
        LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/out
        LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/out
    )
endfunction()

add_ffgl_plugin(LaserBeams com.qmled.laserbeams)
add_ffgl_plugin(LedBorder  com.qmled.ledborder)
