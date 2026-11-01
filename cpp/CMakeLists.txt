cmake_minimum_required(VERSION 3.16)
project(flowrix_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# --- build flavours ----------------------------------------------------------
# Two, chosen with -DFLIX_BUILD=dev or -DFLIX_BUILD=release:
#
#   dev      what you build while working on the game. Still optimised: the
#            native client rasterises every pixel on the CPU, and an -O0 build
#            does not reach a frame rate you can judge anything by. The web
#            client always emits browser Canvas2D operations instead. Frame
#            pointers are kept so a profile has call stacks.
#   release  what ships. -O3, no debug info, asserts and the web build's
#            runtime checks compiled out.
#
# The flavour is the knob; CMAKE_BUILD_TYPE follows from it rather than being
# set alongside it, so there is only one way to say which build this is.
set(FLIX_BUILD "dev" CACHE STRING "Build flavour: dev or release")
set_property(CACHE FLIX_BUILD PROPERTY STRINGS dev release)

if(FLIX_BUILD STREQUAL "release")
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Follows FLIX_BUILD" FORCE)
elseif(FLIX_BUILD STREQUAL "dev")
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Follows FLIX_BUILD" FORCE)
  # RelWithDebInfo defines NDEBUG, which compiles out every assert -- the one
  # thing a development build is most for. Take it back off the flags.
  string(REPLACE "-DNDEBUG" "" CMAKE_CXX_FLAGS_RELWITHDEBINFO
                 "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
  add_compile_options(-fno-omit-frame-pointer)
  # What a development build compiles differently. Today that is two things:
  # the account abuse limits are not enforced (server/account_limits.h), and
  # the web client does not guard against scripted input (client/main.cpp,
  # third_party/cpp_canvas/input_guard.h). It is a compile definition rather
  # than a runtime flag so that a release build cannot be talked into turning
  # them off.
  add_compile_definitions(FLIX_DEV_BUILD)
else()
  message(FATAL_ERROR "FLIX_BUILD must be dev or release, not '${FLIX_BUILD}'")
endif()
message(STATUS "flowrix: ${FLIX_BUILD} build (${CMAKE_BUILD_TYPE})")

add_compile_options(-Wall -Wextra -Wno-unused-parameter)

# --- what the web build is ---------------------------------------------------
# `emcmake cmake ..` sets EMSCRIPTEN and builds THREE targets: the client, as a
# page; the server, as a Node program; and the offline build, which is both of
# them in one page. A browser tab cannot listen on a port, but Node can, so the
# server's web build targets Node rather than the page -- see its link step
# below. The offline build's server listens on nothing: its client is in the
# same page and reaches it over an in-page loopback (shared/net/web_channel.h),
# and the whole game ships as one HTML file that opens from disk. The tools and
# the test binary are the only native-only targets.
#
# FLIX_WEB_BUILD is what the client compiles differently: the chat tags that
# need a document to mean anything are honoured only where there is one. See
# shared/game/markup.h.
#
# The native builds draw and read input through SDL2: from pkg-config on a Mac
# or Linux, and on Windows -- which is only ever cross-compiled, from
# cmake/windows-x86-zig.cmake -- from SDL's own MinGW development package,
# linked statically so the program is one .exe with no SDL2.dll beside it.
# FLIX_SDL2_DIR is that package's i686-w64-mingw32 directory; see
# scripts/build-windows.js, which fetches it.
if(NOT EMSCRIPTEN)
  add_library(flix_sdl2 INTERFACE)
  if(WIN32)
    set(FLIX_SDL2_DIR "" CACHE PATH "SDL2 MinGW development package (its i686-w64-mingw32 directory)")
    if(NOT EXISTS "${FLIX_SDL2_DIR}/include/SDL2/SDL.h" OR NOT EXISTS "${FLIX_SDL2_DIR}/lib/libSDL2.a")
      message(FATAL_ERROR "FLIX_SDL2_DIR must name SDL2's MinGW package for this target, not '${FLIX_SDL2_DIR}'")
    endif()
    target_include_directories(flix_sdl2 SYSTEM INTERFACE ${FLIX_SDL2_DIR}/include/SDL2)
    # The static library, then the system libraries it calls into.
    target_link_libraries(flix_sdl2 INTERFACE
      ${FLIX_SDL2_DIR}/lib/libSDL2.a
      setupapi winmm imm32 version ole32 oleaut32 gdi32 user32 shell32 uuid advapi32)
    # main() is the program's own: SDL's would rename it SDL_main and expect
    # its own WinMain to call it.
    target_compile_definitions(flix_sdl2 INTERFACE SDL_MAIN_HANDLED)
  else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    # FLIX_STATIC_SDL2 links SDL's static library into the programs instead,
    # with the system frameworks it needs, so a packaged build has no
    # libSDL2.dylib to carry beside it (scripts/build-native.js turns it on).
    # The library is named by its full path: -lSDL2 would take the dylib.
    option(FLIX_STATIC_SDL2 "Link SDL2 statically (packaged native builds)" OFF)
    if(FLIX_STATIC_SDL2)
      if(NOT EXISTS "${SDL2_LIBDIR}/libSDL2.a")
        message(FATAL_ERROR "FLIX_STATIC_SDL2: no libSDL2.a in ${SDL2_LIBDIR}")
      endif()
      set(FLIX_SDL2_SYSTEM_LIBS)
      foreach(flag IN LISTS SDL2_STATIC_LDFLAGS)
        if(NOT flag MATCHES "^-L" AND NOT flag STREQUAL "-lSDL2")
          list(APPEND FLIX_SDL2_SYSTEM_LIBS ${flag})
        endif()
      endforeach()
      target_include_directories(flix_sdl2 SYSTEM INTERFACE ${SDL2_INCLUDE_DIRS})
      target_compile_options(flix_sdl2 INTERFACE ${SDL2_CFLAGS_OTHER})
      target_link_libraries(flix_sdl2 INTERFACE ${SDL2_LIBDIR}/libSDL2.a ${FLIX_SDL2_SYSTEM_LIBS})
    else()
      target_link_libraries(flix_sdl2 INTERFACE PkgConfig::SDL2)
    endif()
  endif()
endif()

# --- what the Windows build is -----------------------------------------------
# 32-bit x86, so the one .exe runs on 32- and 64-bit Windows alike, and written
# against Windows 7's API, so nothing newer is called by accident. (The C++
# runtime's threads need Vista at the least; Windows 7 is the floor promised.)
# windows.h's min and max macros would break every std::min in the tree.
if(WIN32)
  add_compile_definitions(_WIN32_WINNT=0x0601 WINVER=0x0601 NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# --- what the web client draws with ------------------------------------------
# The browser build's Canvas has two backends, chosen with -DFLIX_RENDERER:
#
#   canvas2d  the page's own Canvas2D, fed a batched op stream
#             (third_party/cpp_canvas/canvas.cpp). The default, and what ships.
#   skia      Skia's GPU renderer in a WebGL context on the page's canvas
#             (third_party/cpp_canvas/canvas_skia.cpp), linked from a prebuilt
#             static Skia for wasm32-emscripten; see FLIX_SKIA_DIR below.
#
# The client code is the same either way: both are the one Canvas API. A
# native build always draws with the software rasterizer, and the prebuilt
# Skia is wasm only, so asking a native build for Skia is an error.
set(FLIX_RENDERER "canvas2d" CACHE STRING "Web client renderer: canvas2d or skia")
set_property(CACHE FLIX_RENDERER PROPERTY STRINGS canvas2d skia)
if(NOT FLIX_RENDERER STREQUAL "canvas2d" AND NOT FLIX_RENDERER STREQUAL "skia")
  message(FATAL_ERROR "FLIX_RENDERER must be canvas2d or skia, not '${FLIX_RENDERER}'")
endif()
if(FLIX_RENDERER STREQUAL "skia" AND NOT EMSCRIPTEN)
  message(FATAL_ERROR "FLIX_RENDERER=skia is for the web build: configure with emcmake")
endif()
message(STATUS "flowrix: web renderer ${FLIX_RENDERER}")

if(FLIX_RENDERER STREQUAL "skia")
  # The libraries: rust-skia's prebuilt binaries for wasm32-unknown-emscripten,
  # release 0.153.3 (Skia m153; features gl, svg, textlayout, webp, pdf; built
  # with emscripten 4.0.6, -msimd128 and wasm longjmp), unpacked as they come.
  # They are not part of the repository.
  set(FLIX_SKIA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../skia-wasm" CACHE PATH
      "Directory holding the prebuilt Skia static libraries for wasm")
  if(NOT EXISTS "${FLIX_SKIA_DIR}/libskia.wasm.a")
    message(FATAL_ERROR
      "FLIX_RENDERER=skia needs the prebuilt Skia: no libskia.wasm.a in ${FLIX_SKIA_DIR}. "
      "Point -DFLIX_SKIA_DIR at the unpacked rust-skia wasm32-unknown-emscripten binaries.")
  endif()

  # The headers. The prebuilt carries none, and they have to be the ones it was
  # compiled from: rust-skia's fork of Skia at the tag its skia-bindings 0.153.3
  # names in [package.metadata]. Fetched once into the build tree, pinned like
  # the font, and only the two directories a program compiling against Skia
  # reads are kept.
  set(FLIX_SKIA_TAG m153-0.101.2)
  set(FLIX_SKIA_HEADERS ${CMAKE_CURRENT_BINARY_DIR}/skia-${FLIX_SKIA_TAG})
  if(NOT EXISTS "${FLIX_SKIA_HEADERS}/include/core/SkCanvas.h")
    if(CMAKE_VERSION VERSION_LESS 3.18)
      message(FATAL_ERROR "FLIX_RENDERER=skia needs CMake 3.18 or newer to unpack the Skia headers")
    endif()
    set(skia_archive ${CMAKE_CURRENT_BINARY_DIR}/skia-${FLIX_SKIA_TAG}.tar.gz)
    message(STATUS "flowrix: fetching the Skia ${FLIX_SKIA_TAG} headers (about 70MB, once)")
    file(DOWNLOAD "https://codeload.github.com/rust-skia/skia/tar.gz/${FLIX_SKIA_TAG}" "${skia_archive}"
         EXPECTED_HASH SHA256=30c8b51a0a32fae63562c303bdc91605519a9598951422610e3fb8685b3c4f6f
         TLS_VERIFY ON STATUS skia_status)
    list(GET skia_status 0 skia_code)
    list(GET skia_status 1 skia_message)
    if(NOT skia_code EQUAL 0)
      file(REMOVE "${skia_archive}")
      message(FATAL_ERROR "Could not fetch the Skia ${FLIX_SKIA_TAG} headers: ${skia_message}")
    endif()
    file(ARCHIVE_EXTRACT INPUT "${skia_archive}" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}"
         PATTERNS "skia-${FLIX_SKIA_TAG}/include" "skia-${FLIX_SKIA_TAG}/modules/skcms")
    file(REMOVE "${skia_archive}")
  endif()

  # The prebuilt's setjmp/longjmp is wasm's own, which emscripten will not
  # link beside its JavaScript exception throwing -- so nothing in this build
  # may throw, and everything is compiled with -fno-exceptions. That changes
  # nothing a program could see: an emscripten build never catches (exception
  # catching is off by default), so a throw has always ended the program, and
  # under -fno-exceptions the same failure aborts it instead. The whole build
  # rather than the client's half, because the offline page links the server
  # into the same wasm.
  add_compile_options(-fno-exceptions)
  add_link_options(-fno-exceptions)
endif()

# --- cpp_canvas: the vendored Canvas2D-alike renderer ------------------------
# Upstream ships it as a demo executable, whose build files are not vendored;
# built as a library here, straight from its sources, so both the client and
# the render tools can link it.
if(FLIX_RENDERER STREQUAL "skia")
  set(FLIX_CANVAS_BACKEND third_party/cpp_canvas/canvas_skia.cpp)
else()
  set(FLIX_CANVAS_BACKEND third_party/cpp_canvas/canvas.cpp)
endif()
add_library(cpp_canvas STATIC
  ${FLIX_CANVAS_BACKEND}
  third_party/cpp_canvas/path2d.cpp
  third_party/cpp_canvas/svg.cpp
  third_party/cpp_canvas/font.cpp
  third_party/cpp_canvas/file_source.cpp
  third_party/cpp_canvas/window.cpp
  third_party/cpp_canvas/touch_gesture.cpp
)
target_include_directories(cpp_canvas PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cpp_canvas)
target_compile_options(cpp_canvas PRIVATE -w)   # third-party: not ours to lint
if(FLIX_RENDERER STREQUAL "skia")
  # PUBLIC: canvas.h and the client both branch on it.
  target_compile_definitions(cpp_canvas PUBLIC CPP_CANVAS_SKIA)
  # The headers are only ever read by canvas_skia.cpp -- nothing of Skia is in
  # canvas.h -- and they must see what the prebuilt was compiled with: the
  # public defines of a release (is_official_build) wasm build with WebGL
  # Ganesh. SK_RELEASE outright, because a dev build here takes NDEBUG off and
  # Skia would otherwise read that as a debug build, which lays some of its
  # classes out differently.
  target_include_directories(cpp_canvas PRIVATE ${FLIX_SKIA_HEADERS})
  target_compile_definitions(cpp_canvas PRIVATE
    SK_RELEASE SK_GANESH SK_GL SK_ASSUME_WEBGL=1 SKVX_DISABLE_SIMD SK_FORCE_8_BYTE_ALIGNMENT
    SK_DISABLE_TRACING SK_CODEC_DECODES_BMP SK_CODEC_DECODES_WBMP)
  target_compile_options(cpp_canvas PRIVATE "-msimd128")
  # Link order: Skia, then what it calls into. A static archive only gives up
  # the objects something asks for, so the codecs nothing here uses cost
  # nothing.
  target_link_libraries(cpp_canvas PUBLIC
    ${FLIX_SKIA_DIR}/libskia.wasm.a
    ${FLIX_SKIA_DIR}/libfreetype2.wasm.a
    ${FLIX_SKIA_DIR}/libpng.wasm.a
    ${FLIX_SKIA_DIR}/libjpeg.wasm.a
    ${FLIX_SKIA_DIR}/libwebp.wasm.a
    ${FLIX_SKIA_DIR}/libwuffs.wasm.a
    ${FLIX_SKIA_DIR}/libskcms.wasm.a
    ${FLIX_SKIA_DIR}/libexpat.wasm.a
    ${FLIX_SKIA_DIR}/libzlib.wasm.a)
  # WebGL 2 with a WebGL 1 fallback, and the longjmp the prebuilt was compiled
  # for (FreeType's rasterizer and the image codecs use setjmp).
  target_link_options(cpp_canvas PUBLIC "-sMAX_WEBGL_VERSION=2" "-sSUPPORT_LONGJMP=wasm")
endif()
# SDL is desktop-only and attached here rather than to the whole directory.
# PUBLIC lets it reach the native client sources that open links through SDL.
# Browser rendering and input use Canvas2D and DOM APIs directly, so the web
# artifact does not link Emscripten's SDL/WebGL compatibility layer at all.
if(NOT EMSCRIPTEN)
  target_link_libraries(cpp_canvas PUBLIC flix_sdl2)
  # The software rasterizer splits a large fill across scanline bands on a
  # small thread pool. Only the native build has that code at all -- the web
  # build draws through the browser's own context -- so the dependency is
  # attached on the same terms SDL is.
  find_package(Threads REQUIRED)
  target_link_libraries(cpp_canvas PUBLIC Threads::Threads)
endif()

# --- shared: engine core linked by both binaries -----------------------------
add_library(flix_shared STATIC
  shared/core/world.cpp
  shared/core/embedded_files.cpp
  shared/core/json.cpp
  shared/core/process_stats.cpp
  shared/net/transport.cpp
  shared/net/web_channel.cpp
  shared/net/admin_db.cpp
  shared/net/admin_dashboard.cpp
  shared/game/rarity.cpp
  shared/game/config.cpp
  shared/game/skin_format.cpp
  shared/game/skin_likeness.cpp
  shared/game/map_elements.cpp
  shared/game/terrain.cpp
  shared/game/tiled_map.cpp
  shared/game/spatial.cpp
  shared/game/chat_images.cpp
  shared/game/html_entities.cpp
  shared/game/markup.cpp
)
target_include_directories(flix_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# The transport's sockets are Winsock's there.
if(WIN32)
  target_link_libraries(flix_shared PUBLIC ws2_32)
endif()

# --- server ------------------------------------------------------------------
add_library(flix_server STATIC
  server/game_server.cpp
  server/account_limits.cpp
  server/account_search.cpp
  server/admin_dashboard.cpp
  server/admin_db.cpp
  server/admin_db_key.cpp
  server/bot_ai.cpp
  server/auto_update.cpp
  server/chat_commands.cpp
  server/chat_moderation.cpp
  server/replication.cpp
  server/session.cpp
  server/sacrifice.cpp
  server/splitter.cpp
  server/squads.cpp
  server/db.cpp
  server/crypto.cpp
  server/systems/movement.cpp
  server/systems/mob_ai.cpp
  server/systems/petals.cpp
  server/systems/combat.cpp
  server/systems/spawning.cpp
  server/systems/mode_spawning.cpp
  server/systems/loot.cpp
  server/systems/npcs.cpp
  server/systems/dungeons.cpp
)
target_link_libraries(flix_server PUBLIC flix_shared)

# Not for Windows: the dedicated server is a Linux/Mac (and Node) program, and
# the one Windows build that runs a server is the offline one below.
if(NOT WIN32)
  add_executable(flowrix_server server/main.cpp)
  target_link_libraries(flowrix_server PRIVATE flix_server)
endif()

# --- client ------------------------------------------------------------------
add_library(flix_client STATIC
  client/app.cpp
  client/app_login.cpp
  client/app_lobby.cpp
  client/app_game.cpp
  client/app_hud.cpp
  client/app_minimap.cpp
  client/app_chat.cpp
  client/chat_bubbles.cpp
  client/net_client.cpp
  client/world_view.cpp
  client/ui/draw.cpp
  client/ui/text.cpp
  client/ui/face_metrics.cpp
  client/ui/text_cache.cpp
  client/ui/text_atlas.cpp
  client/ui/text_input.cpp
  client/ui/text_select.cpp
  client/ui/context_menu.cpp
  client/ui/remote_image.cpp
  client/ui/menu_style.cpp
  client/ui/menu_widgets.cpp
  client/ui/item_tile.cpp
  client/ui/menus.cpp
  client/ui/menu_inventory.cpp
  client/ui/menu_slot_card.cpp
  client/ui/menu_absorb.cpp
  client/ui/menu_crafting.cpp
  client/ui/menu_oracle.cpp
  client/ui/menu_trade.cpp
  client/ui/menu_assembler.cpp
  client/ui/menu_titan.cpp
  client/ui/menu_gallery.cpp
  client/ui/menu_guild.cpp
  client/ui/menu_admin_dashboard.cpp
  client/ui/menu_admin_db.cpp
  client/ui/menu_assets.cpp
  client/ui/asset_files.cpp
  client/ui/menu_talents.cpp
  client/ui/menu_shop.cpp
  client/ui/menu_skins.cpp
  client/ui/menu_leaderboard.cpp
  client/ui/menu_notifications.cpp
  client/ui/menu_account.cpp
  client/ui/menu_changelog.cpp
  client/ui/menu_settings.cpp
  client/ui/mobile_controls.cpp
  client/ui/touch_scroll.cpp
  client/ui/tutorial.cpp
  client/ui/xp_desktop.cpp
  client/render/art_cache.cpp
  client/render/darkness.cpp
  client/render/mob_art.cpp
  client/render/skin_render.cpp
  client/render/sprites.cpp
  client/render/world_renderer.cpp
  client/web/audio.cpp
  client/web/boot.cpp
  client/web/reload.cpp
)
target_link_libraries(flix_client PUBLIC flix_shared cpp_canvas)

add_executable(flowrix_client client/main.cpp)
target_link_libraries(flowrix_client PRIVATE flix_client)

# The native offline build: server and client in one program, the desktop's
# counterpart of the offline page (that one is the EMSCRIPTEN flowrix_offline
# below; a tree is one or the other, never both). See offline/native_main.cpp.
if(NOT EMSCRIPTEN)
  add_executable(flowrix_offline offline/native_main.cpp)
  target_link_libraries(flowrix_offline PRIVATE flix_server flix_client)
endif()

# A game, not a console program: no terminal window opens beside it. Spelled
# as the linker's flag, which Zig's driver honours where it ignores GCC's
# -mwindows (what WIN32_EXECUTABLE would add). A release is stripped, too: the
# symbols are most of the size of a static .exe.
if(WIN32)
  target_link_options(flowrix_client PRIVATE -Wl,--subsystem,windows)
  target_link_options(flowrix_offline PRIVATE -Wl,--subsystem,windows)
  if(FLIX_BUILD STREQUAL "release")
    target_link_options(flowrix_client PRIVATE -s)
    target_link_options(flowrix_offline PRIVATE -s)
  endif()
endif()

# Browser-only: the settings and session files are the client's whole writable
# state, and MEMFS does not survive a reload. See client/web/persist.cpp.
if(EMSCRIPTEN)
  target_sources(flix_client PRIVATE client/web/persist.cpp)
endif()

# --- runtime data ------------------------------------------------------------
# The browser build loads this exact face from Google Fonts. Stage a pinned
# copy for the native client so its typography does not depend on whatever
# sans-serif happens to be installed on the host OS. Bold only, like gardn:
# every label in the client is set in it.
set(FLIX_DATA_DIR ${CMAKE_CURRENT_BINARY_DIR}/data)
file(MAKE_DIRECTORY ${FLIX_DATA_DIR})
set(FLIX_GOOGLE_FONTS_REV ade3d1533e06b2b1462ffcde8e08b129627ca360)
set(FLIX_GOOGLE_FONTS_ROOT
    "https://raw.githubusercontent.com/google/fonts/${FLIX_GOOGLE_FONTS_REV}/ufl/ubuntu")

function(flix_stage_font filename sha256)
  set(destination "${FLIX_DATA_DIR}/${filename}")
  if(EXISTS "${destination}")
    file(SHA256 "${destination}" existing_hash)
    if(existing_hash STREQUAL sha256)
      return()
    endif()
  endif()
  file(DOWNLOAD "${FLIX_GOOGLE_FONTS_ROOT}/${filename}" "${destination}"
       EXPECTED_HASH "SHA256=${sha256}" TLS_VERIFY ON STATUS download_status)
  list(GET download_status 0 download_code)
  list(GET download_status 1 download_message)
  if(NOT download_code EQUAL 0)
    message(FATAL_ERROR "Could not stage ${filename}: ${download_message}")
  endif()
endfunction()

flix_stage_font(Ubuntu-Bold.ttf
               679b5c1e09cab3156bb8ef529735f9382bf31ca7ac737382ab959297f8d82ad4)

# What every run measures -- each character's advance and the em box -- is
# fixed by that file, so the build works it out once and compiles it into the
# client (client/ui/face_metrics.h) instead of the client reading
# the face at run time or asking the browser. tools/font_table.cpp writes the
# table, through the same Font reader the native client loads the file with.
# It is a build step, not a configure one, so the table cannot outlive the
# face it was made from: natively the tool runs as it is, and on the web it is
# compiled for, and run under, the Node the emscripten toolchain found (CMake
# puts CMAKE_CROSSCOMPILING_EMULATOR in front of a target it runs).
#
# The web client never draws with the face either -- the browser sets every
# run in the Ubuntu the page links from Google Fonts -- so the table is all of
# it the client carries. The TTF itself is embedded only in the offline page,
# which has nowhere to fetch a face from and registers this one with the
# document, and in a Skia client (FLIX_RENDERER=skia), which sets text itself.
# See the link options under EMSCRIPTEN below.
#
# A cross-compiled native build (Windows) cannot run a tool it compiled, and
# has no emulator to run it under. It takes the host's own build of the tool
# instead, as FLIX_FONT_TABLE_TOOL: the table depends on the font file alone,
# so any build of the tool writes the same one.
set(FLIX_FONT_TABLE_TOOL "" CACHE FILEPATH "A host build of tools/font_table.cpp, for cross builds")
if(CMAKE_CROSSCOMPILING AND NOT EMSCRIPTEN)
  if(NOT EXISTS "${FLIX_FONT_TABLE_TOOL}")
    message(FATAL_ERROR "A cross build needs FLIX_FONT_TABLE_TOOL: a host build of tools/font_table.cpp")
  endif()
  set(FLIX_FONT_TABLE_COMMAND ${FLIX_FONT_TABLE_TOOL})
else()
  add_executable(font_table tools/font_table.cpp)
  target_link_libraries(font_table PRIVATE cpp_canvas)
  target_include_directories(font_table PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  set(FLIX_FONT_TABLE_COMMAND font_table)
endif()
if(EMSCRIPTEN)
  if(NOT CMAKE_CROSSCOMPILING_EMULATOR)
    message(FATAL_ERROR "The emscripten toolchain found no Node to run tools/font_table.cpp")
  endif()
  # A command-line program: it reads and writes the host's own files.
  target_link_options(font_table PRIVATE
    "-sENVIRONMENT=node" "-sNODERAWFS=1" "-sEXIT_RUNTIME=1" "-sALLOW_MEMORY_GROWTH=1")
endif()
set(FLIX_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${FLIX_GENERATED_DIR})
set(FLIX_FACE_TABLE ${FLIX_GENERATED_DIR}/face_metrics_table.inc)
add_custom_command(
  OUTPUT ${FLIX_FACE_TABLE}
  COMMAND ${FLIX_FONT_TABLE_COMMAND} ${FLIX_DATA_DIR}/Ubuntu-Bold.ttf ${FLIX_FACE_TABLE}
  DEPENDS ${FLIX_FONT_TABLE_COMMAND} ${FLIX_DATA_DIR}/Ubuntu-Bold.ttf
  COMMENT "Tabulating the font's metrics"
  VERBATIM)
target_sources(flix_client PRIVATE ${FLIX_FACE_TABLE})
target_include_directories(flix_client PRIVATE ${FLIX_GENERATED_DIR})

# The repository's data/ (mobs.json, petals.json, mob_drops.json) and maps/ are
# the single source of truth; stage them beside the binaries so both programs
# find one content directory.
#
# Modelled as a custom COMMAND with real outputs rather than a custom TARGET
# that always runs, because the web link has to be able to DEPEND on the
# staged files. See FLIX_DATA_STAGED's use under EMSCRIPTEN below: the wasm
# embeds this directory at link time, and a target-level dependency only
# ORDERS the two, it does not make the linked wasm out of date when a data
# file changes. That is how an edited mobs.json could be staged correctly and
# still never reach the shipped build -- the link was skipped as up to date and
# the wasm kept the copy it was born with.
# The maps, out of maps/maps.json. That file is the authority on which maps
# exist and on the ORDER that decides which realm each one is; globbing *.tmj
# would stage a map nobody listed and leave the order to the file system.
#
# Read at configure time. An edit to an existing map is picked up by the
# staging command below, which depends on each file; a NEW entry in the
# manifest is picked up because the manifest itself is a configure
# dependency, so the next `make` re-runs cmake and stages it -- without this
# a map added to the list stays unstaged, the server refuses to start on the
# missing file, and every harness test fails on `h.ready`.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             ${CMAKE_CURRENT_SOURCE_DIR}/../maps/maps.json)
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/../maps/maps.json FLIX_MAP_MANIFEST)
string(REGEX MATCHALL "\"file\"[ \t]*:[ \t]*\"[^\"]+\"" FLIX_MAP_ENTRIES "${FLIX_MAP_MANIFEST}")
set(FLIX_MAP_FILES)
foreach(entry IN LISTS FLIX_MAP_ENTRIES)
  string(REGEX REPLACE "^.*:[ \t]*\"([^\"]+)\"$" "\\1" name "${entry}")
  list(APPEND FLIX_MAP_FILES ${CMAKE_CURRENT_SOURCE_DIR}/../maps/${name})
endforeach()
if(NOT FLIX_MAP_FILES)
  message(FATAL_ERROR "maps/maps.json names no maps")
endif()
message(STATUS "flowrix: staging maps ${FLIX_MAP_FILES}")

# Every tileset the manifest's maps name. A Tiled map references its external
# tilesets by a path relative to itself, so the readers resolve them beside the
# staged .tmj -- which only works if they are staged too. Read out of the maps
# at configure time rather than globbed, for the same reason the maps come out
# of the manifest: a .tsj no map names is not map data, and one a map names but
# the tree lacks should fail the build rather than the server.
set(FLIX_MAP_TILESETS)
foreach(map IN LISTS FLIX_MAP_FILES)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${map})
  file(READ ${map} FLIX_MAP_TEXT)
  string(REGEX MATCHALL "\"source\"[ \t]*:[ \t]*\"[^\"]+\"" FLIX_TILESET_REFS "${FLIX_MAP_TEXT}")
  foreach(ref IN LISTS FLIX_TILESET_REFS)
    string(REGEX REPLACE "^.*:[ \t]*\"([^\"]+)\"$" "\\1" source "${ref}")
    get_filename_component(name "${source}" NAME)
    if(name MATCHES "\\.tsj$")
      list(APPEND FLIX_MAP_TILESETS ${CMAKE_CURRENT_SOURCE_DIR}/../maps/${name})
    endif()
  endforeach()
endforeach()
if(NOT FLIX_MAP_TILESETS)
  message(FATAL_ERROR "no map in maps/maps.json names a tileset")
endif()
list(REMOVE_DUPLICATES FLIX_MAP_TILESETS)
message(STATUS "flowrix: staging tilesets ${FLIX_MAP_TILESETS}")

# The tile art: one SVG per tile of every tileset above. Globbed, unlike the
# maps, because the set is defined by the tilesets rather than by a list anyone
# maintains by hand, and a file the tileset names but the directory lacks is a
# warning in the client rather than a broken build.
file(GLOB FLIX_TILE_ART CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../maps/tiles/*.svg)

# The nine ground artworks. No longer map data -- the map's own bottom layer is
# the ground now -- but the title screen still paints its backdrop with them
# (App::titleBackground loads land.svg, desert.svg and the rest by bare name),
# so they still ship. Globbed: the backdrop picks one by name at run time and a
# missing one is a blank backdrop, not a broken build.
file(GLOB FLIX_GROUND_ART CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../maps/ground/*.svg)

set(FLIX_DATA_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../data/mobs.json
    ${CMAKE_CURRENT_SOURCE_DIR}/../data/petals.json
    ${CMAKE_CURRENT_SOURCE_DIR}/../data/mob_drops.json
    ${CMAKE_CURRENT_SOURCE_DIR}/../maps/maps.json
    # Every map maps.json names. Read out of the manifest rather than globbed,
    # so adding a map is one edit in one file and the build fails loudly if the
    # manifest names one that is not there -- which is better than a server
    # that boots with a realm missing.
    ${FLIX_MAP_FILES}
    # The tilesets those maps name, and the tile art the tilesets name.
    ${FLIX_MAP_TILESETS}
    ${FLIX_TILE_ART}
    # The title screen's backdrops.
    ${FLIX_GROUND_ART})

set(FLIX_DATA_STAGED)
foreach(source IN LISTS FLIX_DATA_SOURCES)
  get_filename_component(name "${source}" NAME)
  list(APPEND FLIX_DATA_STAGED ${FLIX_DATA_DIR}/${name})
endforeach()

# Everything the directory is allowed to contain, as bare names: the files
# staged below, plus the font, which is downloaded into it at configure
# time instead.
# Written out for the prune script -- through a file rather than a command line
# because the list is a few hundred names long.
#
# Rewritten only when it changes, so that a re-configure that stages the same
# set does not date the manifest forward and relink both wasms for nothing.
set(FLIX_DATA_EXPECTED Ubuntu-Bold.ttf)
foreach(source IN LISTS FLIX_DATA_SOURCES)
  get_filename_component(name "${source}" NAME)
  list(APPEND FLIX_DATA_EXPECTED ${name})
endforeach()
set(FLIX_DATA_MANIFEST ${CMAKE_CURRENT_BINARY_DIR}/staged_content.txt)
string(REPLACE ";" "\n" FLIX_DATA_EXPECTED_TEXT "${FLIX_DATA_EXPECTED}")
string(APPEND FLIX_DATA_EXPECTED_TEXT "\n")
set(FLIX_DATA_MANIFEST_EXISTING "")
if(EXISTS ${FLIX_DATA_MANIFEST})
  file(READ ${FLIX_DATA_MANIFEST} FLIX_DATA_MANIFEST_EXISTING)
endif()
if(NOT FLIX_DATA_MANIFEST_EXISTING STREQUAL FLIX_DATA_EXPECTED_TEXT)
  file(WRITE ${FLIX_DATA_MANIFEST} "${FLIX_DATA_EXPECTED_TEXT}")
endif()

# The whole set is staged FLAT, by bare file name, and that is what makes the
# references inside the files resolve. A map names its tilesets as siblings and
# a tileset names its tile art under `tiles/`; both readers look for them beside
# the map, and the client looks up a tile's art by bare name (sprites.cpp), so
# `maps/tileset.tsj` and `maps/tiles/grass_c_0.svg` both land directly in the
# data directory. What the author sees in Tiled is what is drawn, and that is
# only true if the same files ship.
#
# A plain `copy`, not `copy_if_different`: the outputs must come out strictly
# newer than the inputs, or every build would see them as still out of date and
# relink the wasm for nothing. The DEPENDS list is what keeps this from running
# when no data changed.
#
# The prune runs first, and it is what keeps the directory equal to the content
# this tree declares rather than to the union of everything every configuration
# ever staged there. The web link embeds the directory whole, so without it a
# file that stops being content -- a replaced map, art a tileset no longer
# names -- goes on being shipped inside both wasms until someone deletes the
# build directory by hand. See cmake/prune_staged_content.cmake.
add_custom_command(
  OUTPUT ${FLIX_DATA_STAGED}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${FLIX_DATA_DIR}
  COMMAND ${CMAKE_COMMAND}
          -DFLIX_DATA_DIR=${FLIX_DATA_DIR}
          -DFLIX_DATA_MANIFEST=${FLIX_DATA_MANIFEST}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/prune_staged_content.cmake
  COMMAND ${CMAKE_COMMAND} -E copy ${FLIX_DATA_SOURCES} ${FLIX_DATA_DIR}/
  DEPENDS ${FLIX_DATA_SOURCES} ${FLIX_DATA_MANIFEST}
  COMMENT "Staging game content")

add_custom_target(flix_data ALL DEPENDS ${FLIX_DATA_STAGED})
add_dependencies(flowrix_client flix_data)
if(TARGET flowrix_server)
  add_dependencies(flowrix_server flix_data)
endif()
if(NOT EMSCRIPTEN)
  add_dependencies(flowrix_offline flix_data)

  # The native offline build carries the whole content directory inside the
  # executable, so it is one file that runs from anywhere: every staged file
  # (the manifest's list, the font included) written into a C++ source as byte
  # arrays, which offline/native_main.cpp mounts over the data directory
  # (shared/core/embedded_files.h). Rewritten whenever the content changes.
  set(FLIX_EMBEDDED_CONTENT ${FLIX_GENERATED_DIR}/embedded_content.cpp)
  add_custom_command(
    OUTPUT ${FLIX_EMBEDDED_CONTENT}
    COMMAND ${CMAKE_COMMAND}
            -DFLIX_DATA_DIR=${FLIX_DATA_DIR}
            -DFLIX_DATA_MANIFEST=${FLIX_DATA_MANIFEST}
            -DFLIX_EMBEDDED_OUTPUT=${FLIX_EMBEDDED_CONTENT}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_content.cmake
    DEPENDS ${FLIX_DATA_STAGED} ${FLIX_DATA_DIR}/Ubuntu-Bold.ttf ${FLIX_DATA_MANIFEST}
            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_content.cmake
    COMMENT "Embedding the game content in the offline build")
  target_sources(flowrix_offline PRIVATE ${FLIX_EMBEDDED_CONTENT})
endif()

# The Windows build's .exe icon, the one Explorer shows before the program has
# run (the window's own is drawn at run time; see App::start). The same picture
# (WorldRenderer::drawAppIcon, drawn from the staged content), written as an
# .ico by tools/app_icon.cpp -- the host's build of it, for font_table's reason
# -- and handed to the link as a resource script, which Zig compiles into the
# .exe's resources.
if(WIN32)
  set(FLIX_APP_ICON_TOOL "" CACHE FILEPATH "A host build of tools/app_icon.cpp, for the Windows icon")
  if(NOT EXISTS "${FLIX_APP_ICON_TOOL}")
    message(FATAL_ERROR "The Windows build needs FLIX_APP_ICON_TOOL: a host build of tools/app_icon.cpp")
  endif()
  set(FLIX_APP_ICON ${FLIX_GENERATED_DIR}/florr.ico)
  add_custom_command(
    OUTPUT ${FLIX_APP_ICON}
    COMMAND ${FLIX_APP_ICON_TOOL} ${FLIX_DATA_DIR} ${FLIX_APP_ICON}
    DEPENDS ${FLIX_APP_ICON_TOOL} ${FLIX_DATA_STAGED}
    COMMENT "Drawing the icon"
    VERBATIM)
  add_custom_target(flix_app_icon DEPENDS ${FLIX_APP_ICON})
  # Rewritten only when it changes, so a re-configure does not relink. The
  # icon is named bare, beside the script: Zig's resource compiler looks for a
  # file relative to the script, and reads an absolute POSIX path as no file.
  set(FLIX_APP_RC ${FLIX_GENERATED_DIR}/florr.rc)
  set(FLIX_APP_RC_TEXT "1 ICON \"florr.ico\"\n")
  set(FLIX_APP_RC_EXISTING "")
  if(EXISTS ${FLIX_APP_RC})
    file(READ ${FLIX_APP_RC} FLIX_APP_RC_EXISTING)
  endif()
  if(NOT FLIX_APP_RC_EXISTING STREQUAL FLIX_APP_RC_TEXT)
    file(WRITE ${FLIX_APP_RC} "${FLIX_APP_RC_TEXT}")
  endif()
  foreach(program flowrix_client flowrix_offline)
    add_dependencies(${program} flix_app_icon)
    target_link_options(${program} PRIVATE ${FLIX_APP_RC})
    set_property(TARGET ${program} APPEND PROPERTY LINK_DEPENDS ${FLIX_APP_ICON} ${FLIX_APP_RC})
  endforeach()
endif()

# --- the web build's link step -----------------------------------------------
if(EMSCRIPTEN)
  # Content goes INSIDE the wasm, not beside it. --embed-file rather than
  # --preload-file, for three reasons: the client reads mobs.json, petals.json,
  # and the maps and their art synchronously during start(),
  # so there is nothing to gain from a separate fetch it would have to wait for
  # anyway; a single artifact cannot half-deploy, which a .wasm and a stale
  # .data next to it can; and the server build has no page to run a preload
  # from at all. The whole set is about 3MB, the maps most of it -- more of
  # the linked wasm by now than the code is -- but it is nearly all text, and
  # it deflates with the rest of the module in the bundle.wasm.bin the page
  # prefers (scripts/build-web.js).
  #
  # The directory is embedded whole; each target below then excludes what it
  # has no use for. The patterns are matched against the staged files' full
  # paths.
  #
  # Every --embed-file and --exclude-file is written as one SHELL: option.
  # CMake de-duplicates a target's link options, so a second bare
  # "--exclude-file" or "--embed-file" is dropped and the argument after it
  # handed to emcc as an input file.
  set(FLIX_EMBED_DATA "SHELL:--embed-file \"${FLIX_DATA_DIR}@data\"")

  # The debug tools a page load can run instead of the game, embedded at /boot
  # in the two builds with a page (client/web/boot.h). Not game content: no
  # handshake hash covers them and the server has no use for them. Globbed --
  # a script dropped into the directory is a tool, and whatever sits beside it
  # (an emscripten program's .wasm) is a file it can fetch -- and named as
  # link dependencies, so editing any of them relinks.
  file(GLOB FLIX_BOOT_TOOLS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/client/web/boot/*)
  set(FLIX_EMBED_BOOT "SHELL:--embed-file \"${CMAKE_CURRENT_SOURCE_DIR}/client/web/boot@boot\"")

  # Boot tools kept out of the repository: client/web/boot-local/ is ignored by
  # git and embedded, beside the ones above at /boot, in the two OFFLINE pages
  # only. Those are build products nobody commits (dist/offline*.html are
  # ignored too), so a tool dropped there runs in its builder's own copy and
  # never reaches dist/bundle.wasm -- the committed client every player
  # downloads -- whatever it is.
  file(GLOB FLIX_LOCAL_BOOT_TOOLS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/client/web/boot-local/*)
  set(FLIX_EMBED_LOCAL_BOOT "")
  if(FLIX_LOCAL_BOOT_TOOLS)
    set(FLIX_EMBED_LOCAL_BOOT "SHELL:--embed-file \"${CMAKE_CURRENT_SOURCE_DIR}/client/web/boot-local@boot\"")
  endif()

  # The two pages' shells, as emcc is given them: each with the boot script's
  # view of the files (client/web/boot_files.js) written in where it says
  # @FLIX_BOOT_FILES_JS@, so the two pages share one copy of it. Generated at
  # configure time; editing any of the three re-runs the configure.
  file(READ ${CMAKE_CURRENT_SOURCE_DIR}/client/web/boot_files.js FLIX_BOOT_FILES_JS)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
               ${CMAKE_CURRENT_SOURCE_DIR}/client/web/boot_files.js)
  set(FLIX_CLIENT_SHELL ${CMAKE_CURRENT_BINARY_DIR}/shells/client.html)
  set(FLIX_OFFLINE_SHELL ${CMAKE_CURRENT_BINARY_DIR}/shells/offline.html)
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/client/web/shell.html ${FLIX_CLIENT_SHELL} @ONLY)
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/offline/shell.html ${FLIX_OFFLINE_SHELL} @ONLY)

  # The page builds' glue defines createFlowrix(module) and nothing else, and
  # the shells call it with the object the game is to be. Not the default --
  # a glue that takes the page's global Module as its own -- because a boot
  # script may be an emscripten program too, and that one needs the global.
  set(FLIX_WEB_MODULARIZE "-sMODULARIZE=1" "-sEXPORT_NAME=createFlowrix")

  # The embed happens at LINK time, so the linked output is stale the moment a
  # staged data file is newer than it. Without this, editing mobs.json or
  # petals.json changes nothing you can see: the staging succeeds, the link is
  # skipped because no object file moved, and the wasm keeps the data it was
  # linked with. Naming the staged files as link dependencies is what makes a
  # data-only edit force a relink.
  set_property(TARGET flowrix_client APPEND PROPERTY LINK_DEPENDS
               ${FLIX_DATA_STAGED} ${FLIX_BOOT_TOOLS})
  set_property(TARGET flowrix_server APPEND PROPERTY LINK_DEPENDS ${FLIX_DATA_STAGED}
               ${CMAKE_CURRENT_SOURCE_DIR}/server/node/pre_init.js)

  # Shared by both web targets. Memory grows because game state and transient
  # command data are not fixed-size; browser rendering itself stays in the
  # browser's Canvas2D surfaces rather than a Wasm software framebuffer.
  set(FLIX_WEB_LINK_OPTIONS
  "-sEXIT_RUNTIME=0"
  "-sALLOW_MEMORY_GROWTH=1"
  "-sINITIAL_MEMORY=256MB"
  )

  # Emscripten's own checks are the web half of the flavour: out-of-bounds
  # heap access, a stack overflow and a bad pointer passed to the runtime are
  # all silent corruption without them and a named error with them. Stated
  # either way rather than left to whatever the optimisation level implies.
  #
  # The link-time -O is a separate question from the compile-time one: it is
  # what runs wasm-opt over the module and minifies the glue. The client is the
  # half of this build whose frame rate pays for that, so a release link is
  # pinned at -O3 whatever the configuration's own flags say, and a dev link is
  # left to take them -- wasm-opt at -O3 is most of the wait on an incremental
  # relink, and there is nothing to measure in a build being iterated on.
  if(FLIX_BUILD STREQUAL "release")
    list(APPEND FLIX_WEB_LINK_OPTIONS "-sASSERTIONS=0")
    set(FLIX_WEB_CLIENT_OPT "-O3")
  else()
    list(APPEND FLIX_WEB_LINK_OPTIONS "-sASSERTIONS=1" "-sSTACK_OVERFLOW_CHECK=1")
    set(FLIX_WEB_CLIENT_OPT "")
  endif()

  target_compile_definitions(flix_shared PUBLIC FLIX_WEB_BUILD)

  # The web build's artifacts are what dist/ ships, and dist/ is served and run
  # by name: the page is index.html, the script beside it bundle.js, and the
  # program `npm start` runs server.js -- the names the TypeScript build used
  # before the C++ client and server took over, so nothing downstream (nginx,
  # pm2, the autoupdate zipball) has to learn new ones. Emscripten writes the
  # .wasm filename into the glue and the glue's filename into the page, so the
  # rename belongs here, at the link, rather than in a copy step afterwards.
  # Only the web build is renamed: the native binaries stay flowrix_client and
  # flowrix_server, which is what the parity rigs and the docs invoke.
  set_target_properties(flowrix_client PROPERTIES OUTPUT_NAME bundle SUFFIX ".html")
  set_target_properties(flowrix_server PROPERTIES OUTPUT_NAME server)
  target_compile_options(flowrix_client PRIVATE
   "-flto"
   "-fno-rtti"
   "-fno-exceptions"
  )
  target_compile_options(flowrix_server PRIVATE "-flto")
  # -flto on an executable reaches only that executable's own sources: here
  # main.cpp, while the game is in the libraries it links, which arrive as
  # finished wasm and are not optimised across. flix_server is the server's
  # half of them and links into nothing a browser runs but the offline page,
  # which links with -flto too. Release only, as the link's -O3 is: it adds
  # the whole server's LTO codegen to every relink. Measured on a 68-flower
  # world under Node it takes 5% off the tick; doing the same to flix_shared
  # would add about 1% more, and would change the client's code with it.
  if(FLIX_BUILD STREQUAL "release")
    target_compile_options(flix_server PRIVATE "-flto")
  endif()

  # Everything staged but the font, for the Canvas2D client: the page links the
  # same Ubuntu from Google Fonts and the browser draws every run in it, and
  # what the client needs of the face is its metrics, which are compiled in
  # (face_metrics.h). Skia sets text itself, so its client carries the face.
  if(FLIX_RENDERER STREQUAL "skia")
    set(FLIX_CLIENT_FONT_FILES "")
  else()
    set(FLIX_CLIENT_FONT_FILES "SHELL:--exclude-file *.ttf")
  endif()

  target_link_options(flowrix_client PRIVATE
    ${FLIX_WEB_LINK_OPTIONS}
    # Every draw goes through the page's own 2D context, or, built with Skia,
    # through the one WebGL context canvas_skia.cpp makes for itself. Either
    # way there is no SDL or GL compatibility backend in this target.
    "-sENVIRONMENT=web"
    # The newer filesystem, for its pluggable backends: the client mounts one
    # of its own over browser storage so that a reload does not forget the
    # settings and the session token. The legacy FS has no equivalent that
    # works here -- IDBFS is asynchronous and needs an explicit syncfs, and
    # WasmFS's own OPFS backend needs threads, which a client that draws every
    # frame through the page's 2D context cannot have.
    "-sWASMFS"
    "-flto"
    "-msimd128"
    "-sEVAL_CTORS=0"
    ${FLIX_WEB_CLIENT_OPT}
    ${FLIX_EMBED_DATA}
    ${FLIX_CLIENT_FONT_FILES}
    ${FLIX_EMBED_BOOT}
    # For a boot script (client/web/shell.html): the page loads the runtime
    # without starting main() and calls it when the script starts the game,
    # and the script's fetch reads data/, boot/ and persist/ through FS.
    "-sEXPORTED_RUNTIME_METHODS=['callMain','FS']"
    ${FLIX_WEB_MODULARIZE}
    "--shell-file" "${FLIX_CLIENT_SHELL}")
  set_property(TARGET flowrix_client APPEND PROPERTY LINK_DEPENDS "${FLIX_CLIENT_SHELL}")

  # The server runs under Node, where it can listen. It is the same program as
  # the native one -- the same tick, the same systems, the same database -- and
  # differs only in what carries the bytes: net/web_channel.cpp accepts
  # WebSocket and, when a certificate is configured, WebTransport sessions.
  # .js, not .mjs: the emitted module has to be CommonJS, because the server
  # reaches for Node's built-ins -- `fs`, `path`, `crypto`, `http`/`https`, `os`
  # and `child_process` -- with require() from inside EM_JS and EM_ASM. Those
  # are all it requires; the WebSocket server is written out in
  # net/web_channel.cpp rather than taken from npm. The one package it ever
  # loads is @fails-components/webtransport, by dynamic import and only for
  # WebTransport, and without it the server still serves WebSocket -- so what
  # dist/ needs beside it to run is nothing at all.
  target_link_options(flowrix_server PRIVATE
    ${FLIX_WEB_LINK_OPTIONS}
    "-sENVIRONMENT=node"
    # Emscripten's default stack is 64KB, against the 8MB a native thread gets,
    # and the JSON parser and the Json destructor both recurse once per level
    # of the document. The account database nests deeply enough that the
    # default stack runs out part-way through loading a real one -- silently,
    # in a release build, as an out-of-bounds access with no name on it. The
    # parser's own kMaxDepth of 200 is the bound on how far this has to go.
    "-sSTACK_SIZE=8388608"
    # The database is a real file that has to survive a restart, so the one
    # directory it lives in is mounted from the host filesystem. Everything
    # else -- all of the embedded content above -- stays in memory.
    "-sFORCE_FILESYSTEM=1"
    # What a crash leaves in the log: a [crash] report with the error and a
    # deep stack, in place of Node's default, which printed the whole one-line
    # glue. --profiling-funcs keeps the wasm's function names, which a release
    # link otherwise strips, so that stack names the C++ it ran through rather
    # than wasm-function[1234]. It costs the size of the names, and nothing
    # per tick.
    "SHELL:--pre-js ${CMAKE_CURRENT_SOURCE_DIR}/server/node/pre_init.js"
    "--profiling-funcs"
    "-flto"
    "-msimd128"
    "-sEVAL_CTORS=0"
    "-lnodefs.js"
    "-sEXPORTED_RUNTIME_METHODS=['FS']"
    "-O3"
    ${FLIX_EMBED_DATA}
    # The server sets no text.
    "SHELL:--exclude-file *.ttf")

  # --- the offline build -----------------------------------------------------
  # Both halves in one wasm, in one page, in one file. offline/main.cpp starts
  # a GameServer and an App and steps them from one frame callback; the server
  # listens on an in-page port and the client dials it, so the transport, the
  # protocol and both programs are the ones above, unchanged.
  #
  # -sSINGLE_FILE is what makes it one file: the wasm is embedded in the
  # script and the script in the page, and nothing is fetched -- which is the
  # whole requirement, because a page opened from disk (file://) may not fetch
  # a .wasm beside itself. The content is embedded as for the other two, and
  # this is the one build that keeps the font in it: the TTF is registered
  # with the document at startup, so the page loads nothing from the network
  # either. The client still measures from the compiled-in table, as the
  # online one does.
  # WASMFS, for the client's storage-backed mount; the server's stack size,
  # because this wasm carries the server's JSON parser too. The linked output
  # is offline.html; the .js and .wasm that would otherwise sit beside it do
  # not exist.
  #
  # Everything the two offline links share. They differ in one thing -- what
  # they compile the module TO -- and stating the rest once is what keeps the
  # asm.js page the same program as the wasm one rather than a second
  # configuration that drifts.
  set(FLIX_OFFLINE_LINK_OPTIONS
    ${FLIX_WEB_LINK_OPTIONS}
    "-sENVIRONMENT=web"
    "-sWASMFS"
    "-sSINGLE_FILE=1"
    "-sSTACK_SIZE=4194304"
    "-flto"
    "-sEVAL_CTORS=0"
    "-fno-rtti"
    ${FLIX_WEB_CLIENT_OPT}
    ${FLIX_EMBED_DATA}
    ${FLIX_EMBED_BOOT}
    ${FLIX_EMBED_LOCAL_BOOT}
    # The page holds main() back while a boot script runs in its place, and
    # the script's startGame() is what calls it; the script's fetch reads the
    # file system through FS (offline/shell.html).
    "-sEXPORTED_RUNTIME_METHODS=['callMain','FS']"
    ${FLIX_WEB_MODULARIZE}
    "--shell-file" "${FLIX_OFFLINE_SHELL}")

  add_executable(flowrix_offline offline/main.cpp)
  target_link_libraries(flowrix_offline PRIVATE flix_server flix_client)
  add_dependencies(flowrix_offline flix_data)
  set_target_properties(flowrix_offline PROPERTIES OUTPUT_NAME offline SUFFIX ".html")
  set_property(TARGET flowrix_offline APPEND PROPERTY LINK_DEPENDS
               ${FLIX_DATA_STAGED} ${FLIX_BOOT_TOOLS}
               ${FLIX_LOCAL_BOOT_TOOLS})
  set_property(TARGET flowrix_offline APPEND PROPERTY LINK_DEPENDS "${FLIX_OFFLINE_SHELL}")
  target_compile_options(flowrix_offline PRIVATE
   "-flto"
   "-fno-rtti"
  )
  target_link_options(flowrix_offline PRIVATE
    ${FLIX_OFFLINE_LINK_OPTIONS}
    "-msimd128")

  # --- the offline build, without WebAssembly --------------------------------
  # The same page for a browser that has no wasm engine: -sWASM=0 runs the
  # linked module through wasm2js, which rewrites it into the JavaScript the
  # asm.js era compiled to, and the glue carries its own WebAssembly shim so
  # nothing reaches for the engine's. With -sSINGLE_FILE that is still one
  # HTML file that opens from disk -- a couple of megabytes larger than the
  # wasm page, and slower to start and to run, which is the trade for playing
  # at all where wasm is off or missing.
  #
  # Two things this link must NOT have, both of which the wasm page does:
  #   * -msimd128. wasm2js cannot translate SIMD instructions, and the link
  #     fails outright if -flto's codegen emits any. Nothing here writes
  #     intrinsics, so dropping the flag costs nothing but the autovectoriser.
  #   * a place in `all`. wasm2js re-codegens the whole module and takes
  #     minutes, so this target is built by name (`npm run build:offline:asmjs`)
  #     and never as a side effect of building something else.
  #
  # Not built with Skia at all: the prebuilt is compiled with SIMD, which is
  # the first of the two.
  if(NOT FLIX_RENDERER STREQUAL "skia")
    add_executable(flowrix_offline_asmjs EXCLUDE_FROM_ALL offline/main.cpp)
    target_link_libraries(flowrix_offline_asmjs PRIVATE flix_server flix_client)
    add_dependencies(flowrix_offline_asmjs flix_data)
    set_target_properties(flowrix_offline_asmjs PROPERTIES
                          OUTPUT_NAME offline-asmjs SUFFIX ".html")
    set_property(TARGET flowrix_offline_asmjs APPEND PROPERTY LINK_DEPENDS
                 ${FLIX_DATA_STAGED} ${FLIX_BOOT_TOOLS}
                 ${FLIX_LOCAL_BOOT_TOOLS})
    set_property(TARGET flowrix_offline_asmjs APPEND PROPERTY LINK_DEPENDS "${FLIX_OFFLINE_SHELL}")
    target_compile_options(flowrix_offline_asmjs PRIVATE
     "-flto"
     "-fno-rtti"
    )
    target_link_options(flowrix_offline_asmjs PRIVATE
      ${FLIX_OFFLINE_LINK_OPTIONS}
      "-sWASM=0")
  endif()
endif()

# --- tools -------------------------------------------------------------------
# Native only, and not in the Windows cross build, which ships the two
# programs above and nothing it could not run here. svg_smoke and
# raster_parity write a PPM from the headless rasterizer; bot_probe and
# projectile_bench boot the real server; mob_render_bench and hitbox_fit draw
# through the client's own renderer.
if(NOT EMSCRIPTEN AND NOT WIN32)
# Every mob and petal artwork in the staged mobs.json and petals.json, drawn
# through the SVG compiler onto one contact sheet. The staged directory rides
# in as FLIX_TEST_DATA_DIR, as it does for bot_probe and hitbox_fit, and the
# staging runs first, so the sheet is of the content this build ships. See
# the file's own header.
add_executable(svg_smoke tools/svg_smoke.cpp)
target_link_libraries(svg_smoke PRIVATE flix_shared cpp_canvas)
target_compile_definitions(svg_smoke PRIVATE FLIX_TEST_DATA_DIR="${FLIX_DATA_DIR}")
add_dependencies(svg_smoke flix_data)

# The program's icon as a Windows .ico or a Mac .iconset, and a PPM of it to
# look at. The Windows cross build runs the host's build of this one
# (FLIX_APP_ICON_TOOL above); the Mac app's icon is packed from its .iconset
# (scripts/build-native.js).
add_executable(app_icon tools/app_icon.cpp)
target_link_libraries(app_icon PRIVATE flix_client)

# The rasterizer's fingerprint. Build it either side of a canvas.cpp change and
# diff the two PPMs: the coverage maths is not what any of those changes are
# for, so a correct one moves no bytes at all. See the file's own header.
add_executable(raster_parity tools/raster_parity.cpp)
target_link_libraries(raster_parity PRIVATE cpp_canvas)

# The bot controller's fingerprint. Runs a real server with a real bot
# population for a few simulated minutes and prints what they did -- how often
# they had something in reach, how often they walked into it, how much ground
# they covered. A controller change that reads better has to move these.
add_executable(bot_probe tools/bot_probe.cpp)
target_link_libraries(bot_probe PRIVATE flix_server flix_client)
target_compile_definitions(bot_probe PRIVATE FLIX_TEST_DATA_DIR="${FLIX_DATA_DIR}")
add_dependencies(bot_probe flix_data)

# The database editor's account search, run over the database's backups: which
# snapshots an account had something in. Reads them as JSON and writes
# nothing. See the file's own header.
add_executable(backup_search tools/backup_search.cpp)
target_link_libraries(backup_search PRIVATE flix_server)
target_compile_definitions(backup_search PRIVATE FLIX_TEST_DATA_DIR="${FLIX_DATA_DIR}")
add_dependencies(backup_search flix_data)

# What a loadout full of projectile petals costs a server tick, in
# milliseconds -- the figure behind "one flower on full gas lags everyone".
# See the file's own header.
add_executable(projectile_bench tools/projectile_bench.cpp)
target_link_libraries(projectile_bench PRIVATE flix_server flix_client)
target_compile_definitions(projectile_bench PRIVATE FLIX_TEST_DATA_DIR="${FLIX_DATA_DIR}")
add_dependencies(projectile_bench flix_data)

# What one mob type costs the mob layer, in milliseconds -- the figure that
# says whether a mob is drawing something expensive every frame. See the
# file's own header.
add_executable(mob_render_bench tools/mob_render_bench.cpp)
target_link_libraries(mob_render_bench PRIVATE flix_client cpp_canvas)
add_dependencies(mob_render_bench flix_data)

# The visual_scale and visualOffsetX/Y that put each mob's artwork on its
# hitbox, measured through the client's own sprite path. Reads mobs.json from
# the source tree, so an edit is measured without a rebuild. See the file's
# own header.
add_executable(hitbox_fit tools/hitbox_fit.cpp)
target_link_libraries(hitbox_fit PRIVATE flix_client cpp_canvas)
target_compile_definitions(hitbox_fit PRIVATE
  FLIX_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../data"
  FLIX_TEST_DATA_DIR="${FLIX_DATA_DIR}")
add_dependencies(hitbox_fit flix_data)

# --- tests -------------------------------------------------------------------
enable_testing()
# Globbed deliberately: tests are added often and a stale list is a test that
# silently stops running. CONFIGURE_DEPENDS re-globs when the directory changes.
file(GLOB FLIX_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)
add_executable(flix_tests ${FLIX_TEST_SOURCES})
# Tests cover both halves, including the end-to-end ones that run a real
# server and real clients over loopback.
target_link_libraries(flix_tests PRIVATE flix_server flix_client)
target_compile_definitions(flix_tests PRIVATE FLIX_TEST_DATA_DIR="${FLIX_DATA_DIR}")
add_dependencies(flix_tests flix_data)
add_test(NAME flix_tests COMMAND flix_tests)
endif()
