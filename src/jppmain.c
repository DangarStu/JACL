/* jppmain.c --- Standalone JACL preprocessor CLI.
 * (C) 2026 Stuart Allen, distribute and use
 * according to GNU GPL, see file COPYING for details.
 *
 * Runs *only* the preprocessing pass (jpp.c): inline every #include,
 * strip leading/trailing whitespace, stamp "#processed:VERSION" and --
 * with -release -- drop the #debug libraries and XOR-obfuscate from the
 * first non-comment line. The result is written to <prefix>.j2 in the
 * temp directory beside the game (or, failing that, the current
 * directory) and the program exits.
 *
 * Unlike driving a full interpreter (cjacl/jacl) just for its start-up
 * side effect, this tool links no Glk library: it never opens a window,
 * never initialises ncurses, never loads or runs the game. So it needs
 * no terminal, can't hang waiting on one, and emits no escape codes --
 * the right tool for batch-building release .j2 download packages (see
 * etc/build-jaclgames.sh).
 *
 * Because it is built from the current core, INTERPRETER_VERSION is
 * compiled straight into the shared jpp.c -- the "#processed" stamp is
 * always correct and never goes stale the way a prebuilt cjacl does.
 *
 * Like decrypt.c, this standalone tool carries its own copies of the
 * small, stable helpers (create_paths / stripwhite / jacl_obfuscate)
 * rather than linking utils.o -- utils.o references execute(),
 * integer_resolve() and friends, which would drag in the whole
 * interpreter. The preprocessing *engine* (jpp.c) is shared, so the .j2
 * format can never drift from what the interpreter expects.
 */

#include "jacl.h"
#include "language.h"
#include "types.h"
#include "prototypes.h"
#include <string.h>

/* --- The global buffers jpp.c reads and writes. These mirror the
 *     canonical declarations in jacl.c; jpp.c uses snprintf with
 *     explicit caps, so matching the sizes keeps every write in
 *     bounds. text_buffer normally lives in encapsulate.c, but we do
 *     not link that unit (it pulls in the interpreter), so it is
 *     defined here as the sole definition. --- */
char            text_buffer[1024];
char            temp_buffer[1024];
char            error_buffer[1024];
char            game_file[256] = "\0";
char            game_path[256] = "\0";
char            prefix[81] = "\0";
char            processed_file[256] = "\0";
char            include_directory[81] = "\0";
char            temp_directory[81] = "\0";

/* jpp.c owns `release`; -release flips it on. */
extern short int release;

static int jacl_whitespace(int character);

int
main(int argc, char *argv[])
{
	const char     *filename = NULL;
	int             index;

	/* Parse a single game-file argument plus optional flags. */
	for (index = 1; index < argc; index++) {
		if (!strcmp(argv[index], "-release")) {
			release = TRUE;
		} else if (!strcmp(argv[index], "-noencrypt")) {
			/* Accepted for command-line parity with the interpreters.
			 * Obfuscation is driven entirely by -release inside jpp.c,
			 * so this flag is a no-op here. */
		} else if (argv[index][0] == '-') {
			fprintf(stderr, "%s: unknown option '%s'\n", argv[0], argv[index]);
			return 1;
		} else if (filename == NULL) {
			filename = argv[index];
		} else {
			fprintf(stderr, "%s: only one game file may be given\n", argv[0]);
			return 1;
		}
	}

	if (filename == NULL) {
		fprintf(stderr, "usage: %s <game.jacl> [-release] [-noencrypt]\n",
			argc > 0 ? argv[0] : "jpp");
		fprintf(stderr, "  Preprocesses a JACL source file into a self-contained .j2.\n");
		fprintf(stderr, "  -release   drop #debug libraries and obfuscate the output\n");
		return 1;
	}

	/* create_paths() strips the extension from its argument in place and
	 * also writes temp_buffer in the no-directory case, exactly as the
	 * interpreters call it with the global temp_buffer. Follow that
	 * pattern: stage the filename there first. */
	snprintf(temp_buffer, sizeof(temp_buffer), "%s", filename);
	create_paths(temp_buffer);

	if (jpp() == FALSE) {
		fprintf(stderr, "%s: %s\n", argv[0],
			error_buffer[0] ? error_buffer : "preprocessing failed");
		return 1;
	}

	printf("%s\n", processed_file);
	return 0;
}

/* --- Stable helpers duplicated from the runtime, as decrypt.c does.
 *     create_paths here is trimmed to what preprocessing needs: it sets
 *     game_file, game_path, prefix and defaults the include/temp dirs.
 *     It deliberately omits the GLK-only walkthru/bookmark/blorb and the
 *     data directory, none of which jpp.c touches. --- */
void
create_paths(char *full_path)
{
	int             i;
	char           *last_slash;

	/* Keep the full game-file name. */
	strcpy(game_file, full_path);

	last_slash = strrchr(full_path, DIR_SEPARATOR);

	/* Strip the file extension (back to the last '.' before any slash). */
	for (i = (int) strlen(full_path) - 1; i >= 0; i--) {
		if (full_path[i] == DIR_SEPARATOR) {
			break;
		} else if (full_path[i] == '.') {
			full_path[i] = 0;
			break;
		}
	}

	if (last_slash == (char *) NULL) {
		/* Game is in the current directory: no path part. */
		strcpy(prefix, full_path);
		game_path[0] = 0;
		snprintf(temp_buffer, 256, ".%c%s", DIR_SEPARATOR, game_file);
		strncpy(game_file, temp_buffer, 255);
		game_file[255] = 0;
	} else {
		/* Split into directory (with trailing slash) and bare prefix. */
		last_slash++;
		strcpy(prefix, last_slash);
		*last_slash = '\0';
		strcpy(game_path, full_path);
	}

	if (include_directory[0] == 0) {
		snprintf(include_directory, 81, "%s%s", game_path, INCLUDE_DIR);
	}

	if (temp_directory[0] == 0) {
		snprintf(temp_directory, 81, "%s%s", game_path, TEMP_DIR);
	}
}

int
jacl_whitespace(int character)
{
	/* Characters JACL treats as leading/trailing whitespace. */
	switch (character) {
		case ':':
		case '\t':
		case ' ':
			return (TRUE);
		default:
			return (FALSE);
	}
}

char *
stripwhite(char *string)
{
	int             i;

	while (jacl_whitespace(*string)) string++;

	i = strlen(string) - 1;

	while (i >= 0 && ((jacl_whitespace(*(string + i))) || *(string + i) == '\n' || *(string + i) == '\r')) i--;

#ifdef _WIN32
	i++;
	*(string + i) = '\r';
#endif
	i++;
	*(string + i) = '\n';
	i++;
	*(string + i) = '\0';

	return string;
}

/* Same XOR-with-0xFF obfuscation as utils.c, stopping at the line
 * break so jpp.c's fputs keeps the source line-delimited. */
void
jacl_obfuscate(char *string)
{
	int             index, length;

	length = strlen(string);

	for (index = 0; index < length; index++) {
		if (string[index] == '\n' || string[index] == '\r') {
			return;
		}
		string[index] = string[index] ^ 255;
	}
}
