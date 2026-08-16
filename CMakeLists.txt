cmake_minimum_required(VERSION 3.21)

file(READ "VERSION" RCLONE_BROWSER_VERSION)
string(STRIP "${RCLONE_BROWSER_VERSION}" RCLONE_BROWSER_VERSION)

project(rclone-browser
  VERSION ${RCLONE_BROWSER_VERSION}
  DESCRIPTION "Qt6 rclone browser with restic snapshot browsing"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(APPLE)
  enable_language(OBJCXX)
  set(CMAKE_OBJCXX_STANDARD 17)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
endif()

# Treating warnings as errors is a CI concern, not a default-build concern: the
# upstream project pinned -Werror and became unbuildable as compilers added new
# diagnostics. Opt in explicitly instead.
option(RRM_WERROR "Treat compiler warnings as errors" OFF)

if(MSVC)
  add_compile_options(/W4 /wd4100 /wd4189 /MP)
  add_compile_definitions(_UNICODE UNICODE _CRT_SECURE_NO_DEPRECATE)
  if(RRM_WERROR)
    add_compile_options(/WX)
  endif()
else()
  add_compile_options(-Wall -Wextra -pedantic)
  if(RRM_WERROR)
    add_compile_options(-Werror)
  endif()
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/build")

add_subdirectory(src)
