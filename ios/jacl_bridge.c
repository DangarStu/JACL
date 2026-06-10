/* jacl_bridge.c --- glue between the SwiftUI app and the embedded RemGlk terp.
 *
 * The SwiftUI side creates a socketpair() and spawns a thread that calls
 * jacl_bridge_run() with one end. This routine points the terp's stdin/stdout
 * at that socket and runs RemGlk's (renamed) entry point, which drives the
 * whole JSON protocol -- read the init event, run to the next input request,
 * emit an update, block on the next event -- until the game quits.
 *
 * Only stdin/stdout are redirected. Anywhere in the app, log with os_log or
 * stderr, NEVER print() to stdout: it would corrupt the JSON on the socket.
 *
 * Compiled only into the iOS app target (JACL_IOS_EMBED). See jacl_bridge.h.
 */

#include <unistd.h>
#include "jacl_ios.h"
#include "jacl_bridge.h"

/* RemGlk's main() is renamed to remglk_main() under JACL_IOS_EMBED (see
 * remglk/main.c) so it doesn't collide with SwiftUI's generated main().
 * We call it as an ordinary function once stdio is redirected. */
extern int remglk_main(int argc, char *argv[]);

int jacl_bridge_run(const char *gamepath, int io_fd)
{
	/* Tell the start-up shim which .j2 to open. glkunix_startup_code()
	 * reads this back via ios_gamepath (see ios_startup.c). */
	jacl_ios_set_gamepath(gamepath);

	/* Point the terp's JSON I/O at the socket the SwiftUI side holds.
	 * RemGlk reads stdin (rgdata.c) and writes stdout, so redirecting both
	 * fds is all that's needed. */
	if (dup2(io_fd, STDIN_FILENO)  < 0) return -1;
	if (dup2(io_fd, STDOUT_FILENO) < 0) return -1;

	/* Declare the input features the SwiftUI front-end can deliver. RemGlk
	 * parses these as library options, so they are NOT forwarded to
	 * glkunix_startup_code -- startdata stays argc==1 and our shim falls
	 * back to the game path set above. */
	char *argv[] = { "jacl", "-support", "hyperlinks", "-support", "graphics", NULL };
	int   argc   = (int)(sizeof(argv) / sizeof(argv[0])) - 1;

	/* Runs the game to completion. Under JACL_IOS_EMBED glk_exit() calls
	 * pthread_exit(), so on a normal quit this never returns. */
	return remglk_main(argc, argv);
}
