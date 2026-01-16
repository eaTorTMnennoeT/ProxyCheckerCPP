cmake_minimum_required(VERSION 3.15)
project(ProxyChecker VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows specific settings
add_definitions(-DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN)

# Source files
set(SOURCES
    main.cpp
    ProxyChecker.cpp
    ProxyChecker.h
    resource.h
    resource.rc
)

# Create executable
add_executable(${PROJECT_NAME} WIN32 ${SOURCES})

# Link Windows libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    winhttp
    comctl32
    uxtheme
    shlwapi
    comdlg32
    dwmapi
)

# Compiler specific settings
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /O2)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        LINK_FLAGS "/MANIFESTDEPENDENCY:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\""
    )
elseif(MINGW)
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -O2 -static)
    target_link_options(${PROJECT_NAME} PRIVATE -static -mwindows)
endif()
