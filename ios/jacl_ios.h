/* jacl_ios.h --- Public C entry points for the iOS (iPad) host app.
 *
 * This is the *only* header the Swift / Objective-C app shell needs to
 * talk to the JACL core. It deliberately exposes nothing from glk.h, so
 * it can be dropped straight into a bridging header without dragging the
 * Glk type system into Swift.
 *
 * Lifecycle (RemGlk backend), on the interpreter thread (see jacl_bridge.c):
 *
 *     jacl_ios_set_gamepath("/.../Documents/grail.j2");
 *     remglk_main(...);   // -> glkunix_startup_code() -> jacl_ios_prepare()
 *
 * RemGlk drives start-up "the Unix way": its main() calls
 * glkunix_startup_code(), which reads the stored game path and calls
 * jacl_ios_prepare() to open the stream.
 *
 * NOTE: calling jacl_ios_prepare() directly before glk_main() does NOT work
 * under RemGlk -- glkunix_stream_open_pathname() only opens files while
 * RemGlk's `inittime` flag is set, i.e. during glkunix_startup_code()
 * (remglk/main.c:609). Always go through the glkunix entry point.
 */

#ifndef JACL_IOS_H
#define JACL_IOS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Store the absolute path of the .j2 the user picked. Pass NULL to clear.
 * The path is copied; the caller keeps ownership of its buffer. */
void        jacl_ios_set_gamepath(const char *path);

/* The path last handed to jacl_ios_set_gamepath() (empty string if none). */
const char *jacl_ios_gamepath(void);

/* Resolve paths, run the (no-op for .j2) preprocessor, open the game
 * stream and tell Glk where saves live. Safe to call more than once;
 * the work only happens the first time. Returns TRUE (1) always, matching
 * glk_startup.c -- on failure it sets the interpreter's error_buffer and
 * the game window opens to show the message rather than failing silently. */
int         jacl_ios_prepare(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* JACL_IOS_H */
