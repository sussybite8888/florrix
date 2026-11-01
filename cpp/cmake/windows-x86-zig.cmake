# Cross-compiles the native client and the native offline build for 32-bit
# Windows from a Mac (or Linux) with Zig, which carries its own clang, lld and
# MinGW-w64 libraries for the target -- nothing Windows-specific has to be
# installed. scripts/build-windows.js configures a tree with this file.
#
# 32-bit x86 because one i686 .exe runs on 32- and 64-bit Windows both, and
# Windows 7 as the floor (the wrappers' target, x86-windows.win7-gnu): that is
# what makes Zig build libc++ without calls newer than Windows 7 -- left to
# itself it targets Windows 10 and asks for GetSystemTimePreciseAsFileTime,
# which Windows 7 does not have and would refuse to start the .exe over. Zig
# links the Universal C Runtime, which Windows 10 and 11 ship with and Windows
# 7 and 8.1 have from Windows Update (KB2999226) or any Visual C++ 2015+
# redistributable; libc++ is linked statically, so nothing else rides along.
#
# CMake wants each tool as one program, so each is a two-line wrapper around
# the matching zig subcommand, in zig/ beside this file.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86)

set(CMAKE_CXX_COMPILER ${CMAKE_CURRENT_LIST_DIR}/zig/x86-windows-c++)
set(CMAKE_AR ${CMAKE_CURRENT_LIST_DIR}/zig/x86-windows-ar CACHE FILEPATH "Archiver")
set(CMAKE_RANLIB ${CMAKE_CURRENT_LIST_DIR}/zig/x86-windows-ranlib CACHE FILEPATH "Ranlib")

# Nothing of the host's is a library for this target.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
