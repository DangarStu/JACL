/* ios_startup.c --- iOS / sandbox start-up shim for JACL.
 *
 * This is the iPad analogue of glk_startup.c. It is linked *instead of*
 * glk_startup.c (exactly as winglk_startup.c is for the Windows build),
 * so it must define the same start-up globals the core expects
 * (jpp_error, blorb_stream) and supply glkunix_startup_code().
 *
 * Differences from glk_startup.c, all driven by the iOS sandbox:
 *
 *   - The game path comes from the host app -- the .j2 the user picked in
 *     the document picker -- not from argv. jacl_ios_set_gamepath() stores
 *     it; jacl_ios_prepare() does the load.
 *
 *   - Games ship pre-processed (.j2). jpp() therefore short-circuits (see
 *     jpp.c: it spots "#processed" in the first ten lines and returns the
 *     file untouched) and writes nothing. The only files written at run
 *     time are Glk saves, which land in the app sandbox via the Glk
 *     fileref API (see glk_saver.c). No writable temp/ or includes/ dir is
 *     needed on device.
 *
 *   - The same object still implements glkunix_startup_code(), so this
 *     build can be linked against CheapGlk or GlkTerm and driven from the
 *     command line with a .j2 path for testing on the desktop. See
 *     ios/Makefile, target `sim`.
 *
 * Neither GARGLK nor WINGLK is defined for this build, so the blorb opens
 * through glk_fileref_create_by_name() (jacl.c), which resolves against
 * the base file set below -- i.e. inside the sandbox next to the .j2.
 */

#include "jacl.h"
#include "glk.h"
#include "gi_blorb.h"
#include "glkstart.h"
#include "language.h"
#include "prototypes.h"
#include "jacl_ios.h"
#include <string.h>
#include <stdio.h>

/* ---- Start-up globals the core links against (mirrors glk_startup.c) ---- */

int             jpp_error = FALSE;

/* THE STREAM FOR THE ARCHIVE CONTAINING GRAPHICS AND SOUND (opened in
 * glk_main(); declared here because glk_startup.c is the file that owns it
 * in the desktop build and we are its replacement). */
strid_t         blorb_stream;

/* (glk_startup.c also defines a vestigial `short int encrypt;` here, but it
 * is omitted on purpose: nothing in the iOS build reads it, and the name
 * collides with POSIX encrypt() from <unistd.h> on the iOS SDK.) */

/* The game file the host app wants us to run. Set by the app shell before
 * the interpreter thread starts; read by glkunix_startup_code() when a
 * backend drives start-up itself. */
static char     ios_gamepath[1024] = "";

void
jacl_ios_set_gamepath(const char *path)
{
	if (path == NULL) {
		ios_gamepath[0] = '\0';
	} else {
		snprintf(ios_gamepath, sizeof(ios_gamepath), "%s", path);
	}

	/* A new game is being requested into this reused process. game_stream and
	 * blorb_stream are process globals that outlive the previous game's terp --
	 * it exits via pthread_exit() without Glk teardown, leaving these strids
	 * dangling. jacl_ios_prepare() below treats a non-NULL game_stream as
	 * "already loaded" and short-circuits, which would run the PREVIOUS game
	 * again (you pick a new title and the old one loads). Drop the dangling
	 * handles so the new path is actually loaded. The old stream objects are
	 * already orphaned by the abrupt exit; we just release our pointers. */
	game_stream = NULL;
	blorb_stream = NULL;
}

const char *
jacl_ios_gamepath(void)
{
	return ios_gamepath;
}

int
jacl_ios_prepare(const char *path)
{
	/* Idempotent: if we already opened a game stream, a second call (e.g.
	 * the app calling us directly AND the backend running
	 * glkunix_startup_code) is a no-op rather than a double-load. */
	if (game_stream != NULL) {
		return (TRUE);
	}

	if (path == NULL || *path == '\0') {
		snprintf(error_buffer, 1024, "%s^", NO_GAME);
		jpp_error = TRUE;
		/* TRUE so the caller still opens a window to show the error. */
		return (TRUE);
	}

	/* temp_buffer is 1024 bytes; a sandbox path can be long, so copy in
	 * bounded. */
	snprintf(temp_buffer, 1024, "%s", path);

	/* Derive game_path / prefix / blorb / bookmark / save locations.
	 * NOTE (sandbox audit, see ios/README.md): for a pre-processed .j2 the
	 * include/ and temp/ directories create_paths() derives are never
	 * touched, but data_directory and the prefix.blorb path must resolve
	 * inside the app sandbox. Because the .j2 itself lives in the sandbox,
	 * everything create_paths() derives from it does too. */
	create_paths(temp_buffer);

	/* For a .j2 this returns immediately having set processed_file =
	 * game_file and written nothing. For raw .jacl it would try to write a
	 * .j2 into temp_directory -- which on device must be sandbox-writable;
	 * shipping .j2 avoids that entirely. */
	if (jpp() == FALSE) {
		jpp_error = TRUE;
		return (TRUE);
	}

	/* glkunix_stream_open_pathname() is part of the glkunix start-up layer
	 * and is valid to call here (during start-up) on CheapGlk, GlkTerm,
	 * RemGlk and iosglk alike. FALSE = binary mode. */
	game_stream = glkunix_stream_open_pathname(processed_file, FALSE, 0);
	if (!game_stream) {
		strcpy(error_buffer, NOT_FOUND);
		jpp_error = TRUE;
		return (TRUE);
	}

	/* Saves resolve relative to this file -- i.e. the sandbox directory
	 * holding the .j2 (with -gamefiledir, see jacl_bridge.c). */
	glkunix_set_base_file(game_file);

	/* Open the game's blorb (graphics) here, during start-up, where
	 * glkunix_stream_open_pathname() is valid under RemGlk. We can't leave
	 * this to jacl.c's glk_fileref_create_by_name() path: RemGlk appends a
	 * usage suffix (".glkdata") to by-name filerefs (rgfref.c), so
	 * "<prefix>.blorb" would never be found. Opening the full sandbox path
	 * sidesteps that; jacl.c then finds no suffixed file and leaves this map
	 * in place. */
	{
		char    blorb_path[1024];
		strid_t bs;

		snprintf(blorb_path, sizeof(blorb_path), "%s%s.blorb", game_path, prefix);
		bs = glkunix_stream_open_pathname(blorb_path, FALSE, 0);
		if (bs) {
			blorb_stream = bs;
			giblorb_set_resource_map(bs);
		}
	}

	return (TRUE);
}

/* ---- glkunix entry point -------------------------------------------------
 *
 * Used by every backend that drives start-up the Unix way (CheapGlk /
 * GlkTerm for the desktop `sim` build; RemGlk and the iosglk Glulxe
 * harness on device). An iosglk app that prefers to drive start-up itself
 * can ignore this and call jacl_ios_prepare() directly before glk_main().
 */

glkunix_argumentlist_t glkunix_arguments[] = {
	{"", glkunix_arg_ValueFollows, "filename: the .j2 game file to load." },
	{ NULL, glkunix_arg_End, NULL }
};

int
glkunix_startup_code(glkunix_startup_t *data)
{
	const char *path = NULL;

	if (data->argc > 1) {
		/* Desktop sim: path supplied on the command line. */
		path = data->argv[1];
	} else if (*ios_gamepath) {
		/* On device: the app already told us which file to run. */
		path = ios_gamepath;
	}

	return jacl_ios_prepare(path);
}
