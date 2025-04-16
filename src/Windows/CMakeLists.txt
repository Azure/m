cmake_minimum_required(VERSION 3.30)

include(CheckCXXSourceCompiles)
check_cxx_source_compiles([=[
#include <sdkddkver.h>
static_assert(WDK_NTDDI_VERSION >= NTDDI_WIN10_NI, "Inspecting WDK_NTDDI_VERSION, the Windows SDK version.");
int main() {}
]=] WINDOWS_SDK_VERSION_CHECK)

if(NOT WINDOWS_SDK_VERSION_CHECK)
    message(FATAL_ERROR "The STL must be built with the Windows 11 SDK (10.0.22621.0) or later. Make sure it's available by selecting it in the Individual Components tab of the VS Installer.")
endif()

add_compile_definitions(
    _ALLOW_ITERATOR_DEBUG_LEVEL_MISMATCH WIN32_LEAN_AND_MEAN STRICT _CRT_STDIO_ARBITRARY_WIDE_SPECIFIERS
    _WIN32_WINNT=0x0A00 NTDDI_VERSION=NTDDI_WIN10_NI
    NOMINMAX
)

#add_subdirectory(include)
add_subdirectory(libraries)
#add_subdirectory(tools)

