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

/* Return a pointer to image resource `num` from the game's blorb (or NULL if
 * there is no blorb open or no such image), with its byte length in *len. The
 * bytes are the raw image file (PNG/JPEG) as packaged in the blorb. The
 * pointer is owned by the blorb layer -- copy the bytes if you need to keep
 * them. RemGlk sends the image number/size in its JSON; this resolves the
 * number to actual pixels (see ios/README.md, graphics). */
const void *jacl_bridge_image(unsigned int num, unsigned int *len);

/* Raw bytes of sound resource `num` from the game's blorb (Ogg Vorbis, AIFF or
 * MOD), with the length in *len, or NULL if there's no blorb or no such sound.
 * The pointer is owned by the blorb layer -- copy the bytes to keep them. The
 * app sniffs the format from the bytes and plays it. */
const void *jacl_bridge_sound(unsigned int num, unsigned int *len);

/* Decode Ogg Vorbis bytes (a blorb sound) to a self-contained 16-bit PCM WAV
 * in a malloc'd buffer, with the length in *out_len, or NULL on failure. The
 * caller owns the returned buffer and must free() it. AVAudioPlayer can't play
 * Ogg, but it plays the WAV this produces. (Implemented in jacl_audio.c via
 * stb_vorbis.) */
void *jacl_ogg_to_wav(const void *ogg, int ogg_len, int *out_len);

/* The JACL interpreter version, "J_VERSION.J_RELEASE.J_BUILD" (e.g. "4.7.0"),
 * from version.h. The app shows it next to this build's link time so you can
 * confirm at a glance which build is actually running. */
const char *jacl_interpreter_version(void);

/* Called from every terp exit path (glk_exit / fatal / fast_exit) just before
 * pthread_exit(), to release the one-terp-at-a-time gate so the next game's
 * jacl_bridge_run() may proceed. See jacl_bridge.c. */
void jacl_bridge_mark_terp_exited(void);

/* Suppress (1) or allow (0) the silent autosave that otherwise fires when the
 * game's socket closes. The app sets this to 1 right before a Restart closes
 * the socket, so the discarded game isn't autosaved over; each new game's
 * glk_main resets it to 0. Defined in jacl.c (JACL_IOS_EMBED). */
void jacl_autosave_set_suppressed(int suppressed);

#ifdef __cplusplus
}
#endif

#endif /* JACL_BRIDGE_H */
