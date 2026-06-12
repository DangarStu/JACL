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

#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include "glk.h"
#include "gi_blorb.h"
#include "jacl_ios.h"
#include "jacl_bridge.h"
#include "version.h"

/* RemGlk's main() is renamed to remglk_main() under JACL_IOS_EMBED (see
 * remglk/main.c) so it doesn't collide with SwiftUI's generated main().
 * We call it as an ordinary function once stdio is redirected. */
extern int remglk_main(int argc, char *argv[]);

/* J_VERSION.J_RELEASE.J_BUILD from version.h -- shown in the app's shelf so
 * you can confirm which interpreter build is running. */
const char *jacl_interpreter_version(void)
{
    static char buf[32];
    snprintf(buf, sizeof(buf), "%d.%d.%d", J_VERSION, J_RELEASE, J_BUILD);
    return buf;
}

/* ---- one-terp-at-a-time gate --------------------------------------------
 *
 * JACL + RemGlk keep all state in process globals and share the one pair of
 * stdin/stdout the terp dup2()s its socket onto, so only ONE terp may run at a
 * time. The terp ends via pthread_exit() (never returns; the app can't join a
 * glk_exit), and the Swift onDisappear that closes the old socket is
 * unreliable on a navigation pop -- so the previous game's terp can still be
 * alive when the next starts. It then reads the NEW game's socket off the
 * shared fd and renders the OLD game into the new window (the bug: "running
 * grail but showing dragon").
 *
 * This gate closes the hole: jacl_bridge_mark_terp_exited() is called from
 * every terp exit path (rgmisc.c glk_exit / fatal, rgwindow.c fast_exit), and
 * jacl_bridge_run() blocks until the previous terp signals exit before it
 * touches the shared stdio. The wait is bounded so a wedged terp can't hang
 * the app. Combined with the Swift side force-stopping the previous terp when
 * a new game starts, the old terp is guaranteed gone before the new one runs. */
static pthread_mutex_t terp_gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  terp_gone = PTHREAD_COND_INITIALIZER;
static int             terp_live = 0;

void jacl_bridge_mark_terp_exited(void)
{
	pthread_mutex_lock(&terp_gate);
	terp_live = 0;
	pthread_cond_broadcast(&terp_gone);
	pthread_mutex_unlock(&terp_gate);
	fprintf(stderr, "JDBG terp gate: released\n");
}

int jacl_bridge_run(const char *gamepath, int io_fd)
{
	fprintf(stderr, "JDBG jacl_bridge_run gamepath=%s io_fd=%d\n",
	        gamepath ? gamepath : "(null)", io_fd);

	/* Wait for any previous terp to fully exit before claiming the shared
	 * stdin/stdout below -- otherwise two terps race on the same fds and the
	 * older one hijacks this game's window. Bounded so a stuck terp can't
	 * black-hole the app. */
	pthread_mutex_lock(&terp_gate);
	if (terp_live) {
		struct timespec deadline;
		clock_gettime(CLOCK_REALTIME, &deadline);
		deadline.tv_sec += 3;
		fprintf(stderr, "JDBG terp gate: waiting for previous terp\n");
		while (terp_live) {
			if (pthread_cond_timedwait(&terp_gone, &terp_gate, &deadline) != 0) {
				fprintf(stderr, "JDBG terp gate: TIMEOUT, proceeding\n");
				break;
			}
		}
	}
	terp_live = 1;
	pthread_mutex_unlock(&terp_gate);

	/* Tell the start-up shim which .j2 to open. glkunix_startup_code()
	 * reads this back via ios_gamepath (see ios_startup.c). */
	jacl_ios_set_gamepath(gamepath);

	/* Point the terp's JSON I/O at the socket the SwiftUI side holds.
	 * RemGlk reads stdin (rgdata.c) and writes stdout, so redirecting both
	 * fds is all that's needed. */
	if (dup2(io_fd, STDIN_FILENO)  < 0) return -1;
	if (dup2(io_fd, STDOUT_FILENO) < 0) return -1;

	/* stdin/stdout are process-global FILE*s that persist across games in this
	 * single process. The previous game's terp died when its socket closed:
	 * getc() hit EOF (setting stdin's *sticky* EOF flag) and gli_fatal_error()
	 * tried to fflush() a final error stanza onto the now-closed socket -- EPIPE,
	 * so that text is left stuck in stdout's buffer. dup2() above only swaps the
	 * kernel fd; it does NOT touch these userspace stdio flags/buffers. Without
	 * this reset the new terp's first getc(stdin) returns EOF immediately (the
	 * stale sticky flag) and dies before drawing anything -- a black screen --
	 * and the leftover stdout bytes would corrupt this game's opening JSON.
	 * Clear the EOF/error flags and drop any buffered leftover so each game
	 * starts from clean stdio. */
	clearerr(stdin);
	clearerr(stdout);
	fpurge(stdin);
	fpurge(stdout);

	/* RemGlk library options (parsed by remglk_main, NOT forwarded to
	 * glkunix_startup_code -- startdata stays argc==1 and our shim falls
	 * back to the game path set above):
	 *  -gamefiledir : make glkunix_set_base_file() actually set the fileref
	 *                 working dir (rgfref.c gates it on this), so the blorb
	 *                 and save files resolve next to the .j2 in the sandbox
	 *                 rather than against the process cwd.
	 *  -support …   : input/graphics features the SwiftUI front-end provides. */
	char *argv[] = { "jacl", "-gamefiledir", "yes",
	                 "-support", "hyperlinks", "-support", "graphics", NULL };
	int   argc   = (int)(sizeof(argv) / sizeof(argv[0])) - 1;

	/* Runs the game to completion. Under JACL_IOS_EMBED glk_exit() calls
	 * pthread_exit(), so on a normal quit this never returns. */
	return remglk_main(argc, argv);
}

const void *jacl_bridge_image(unsigned int num, unsigned int *len)
{
	giblorb_result_t res;
	giblorb_map_t *map;

	*len = 0;

	/* jacl.c calls giblorb_set_resource_map() when the game's blorb opens;
	 * this returns NULL if the game has no blorb. */
	map = giblorb_get_resource_map();
	if (map == NULL) {
		return NULL;
	}

	if (giblorb_load_resource(map, giblorb_method_Memory, &res,
	                          giblorb_ID_Pict, num) != giblorb_err_None) {
		return NULL;
	}

	*len = (unsigned int) res.length;
	return res.data.ptr;
}
