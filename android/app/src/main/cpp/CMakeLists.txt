# CMakeLists.txt --- native build of the JACL interpreter for Android.
#
# Builds one shared library, libjacl.so, from the EXACT same portable C the
# iPad app runs: the JACL core (../src), RemGlk (../ios/remglk), and the embed
# glue (../ios/ios_startup.c + ../ios/jacl_bridge.c), plus a thin JNI shim
# (android_jni.c). The Kotlin app talks to it over a socketpair exactly as the
# SwiftUI app does -- jacl_bridge.c is plain POSIX and needs no changes.
#
# Reuses ../ios for the embed shim and RemGlk rather than copying them, so the
# two app front-ends stay byte-for-byte on the same interpreter core.

cmake_minimum_required(VERSION 3.22.1)
project(jacl C)

# Repo root, five levels up from app/src/main/cpp.
get_filename_component(JACL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
set(SRC   "${JACL_ROOT}/src")
set(IOS   "${JACL_ROOT}/ios")
set(REMGLK "${IOS}/remglk")

# Shared, portable interpreter core (matches src/Makefile COMMON_SRC + jacl.c
# + glk_saver.c, exactly the iOS Makefile's IOS_SRC list).
set(CORE_SRC
    ${SRC}/findroute.c ${SRC}/interpreter.c ${SRC}/loader.c ${SRC}/logging.c
    ${SRC}/parser.c ${SRC}/display.c ${SRC}/utils.c ${SRC}/jpp.c
    ${SRC}/resolvers.c ${SRC}/errors.c ${SRC}/encapsulate.c ${SRC}/libcsv.c
    ${SRC}/jacl.c ${SRC}/glk_saver.c)

# The iOS build swaps glk_startup.c -> ios_startup.c (JACL's glkunix entry).
set(EMBED_SRC ${IOS}/ios_startup.c ${IOS}/jacl_bridge.c)

# RemGlk objects (matches ios/remglk/Makefile REMGLK_OBJS; cgunigen.c is
# #included by cgunicod.c, not compiled separately).
set(REMGLK_SRC
    ${REMGLK}/main.c ${REMGLK}/rgevent.c ${REMGLK}/rgfref.c ${REMGLK}/rggestal.c
    ${REMGLK}/rgdata.c ${REMGLK}/rgmisc.c ${REMGLK}/rgauto.c ${REMGLK}/rgstream.c
    ${REMGLK}/rgstyle.c ${REMGLK}/rgwin_blank.c ${REMGLK}/rgwin_buf.c
    ${REMGLK}/rgwin_grid.c ${REMGLK}/rgwin_pair.c ${REMGLK}/rgwin_graph.c
    ${REMGLK}/rgwindow.c ${REMGLK}/rgschan.c ${REMGLK}/rgblorb.c
    ${REMGLK}/cgunicod.c ${REMGLK}/cgdate.c ${REMGLK}/gi_dispa.c
    ${REMGLK}/gi_debug.c ${REMGLK}/gi_blorb.c)

add_library(jacl SHARED
    android_jni.c
    ${CORE_SRC}
    ${EMBED_SRC}
    ${REMGLK_SRC})

target_include_directories(jacl PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SRC}
    ${IOS}
    ${REMGLK})

# -DGLK            : core sets up blorb/save paths (create_paths)
# -DNATIVE_LANGUAGE: parser strings baked in (matches the desktop/iOS build)
# -DJACL_IOS_EMBED : the embed behaviour -- remglk_main rename, glk_exit ->
#                    pthread_exit, the one-terp-at-a-time gate. Generic to any
#                    in-app host; reused verbatim for Android.
target_compile_definitions(jacl PRIVATE GLK NATIVE_LANGUAGE=1 JACL_IOS_EMBED)

# The core/RemGlk are old C with the usual unused-result/var warnings; quiet
# them as the desktop and iOS builds do, and don't let them fail the build.
target_compile_options(jacl PRIVATE
    -Wno-unused-result -Wno-unused-but-set-variable -Wno-implicit-function-declaration)

# log for __android_log; the JNI lib is linked automatically by the NDK.
find_library(log-lib log)
target_link_libraries(jacl ${log-lib})
