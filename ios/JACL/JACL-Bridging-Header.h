//  JACL-Bridging-Header.h
//  Exposes the JACL C entry points to Swift. Paths are relative to this
//  file's location (ios/JACL/) so they resolve with or without the target's
//  HEADER_SEARCH_PATHS.

#include "../jacl_bridge.h"   // jacl_bridge_run(gamepath, io_fd)
#include "../jacl_ios.h"      // jacl_ios_set_gamepath / _gamepath / _prepare
