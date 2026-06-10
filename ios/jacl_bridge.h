/* jacl_bridge.h --- the C entry the SwiftUI app calls to run a game.
 *
 * Compiled only into the iOS app target (define JACL_IOS_EMBED). The desktop
 * sims don't use it -- they run RemGlk's own main() directly.
 */

#ifndef JACL_BRIDGE_H
#define JACL_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Run the embedded RemGlk interpreter. Call this on a dedicated background
 * thread, never the main/UI thread.
 *
 *   gamepath : absolute path to the .j2 inside the app sandbox.
 *   io_fd    : one end of a socketpair(); RemGlk's JSON stdin/stdout are
 *              dup2'd onto it. The SwiftUI side keeps the other end and
 *              exchanges JSON events/updates over it.
 *
 * On a normal quit this does NOT return: glk_exit() ends the thread via
 * pthread_exit() (see remglk/rgmisc.c under JACL_IOS_EMBED). It returns a
 * negative value only if the initial stdio redirection fails.
 */
int jacl_bridge_run(const char *gamepath, int io_fd);

#ifdef __cplusplus
}
#endif

#endif /* JACL_BRIDGE_H */
