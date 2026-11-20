/* utils.c --- General utility functions
 * (C) 2008 Stuart Allen, distribute and use 
 * according to GNU GPL, see file COPYING for details.
 */

#ifdef _WIN32
/* MUST PRECEDE <stdlib.h> FOR rand_s(), USED BY secure_random_number() */
#define _CRT_RAND_S
#endif


#include "jacl.h"
#include "language.h"
#include "types.h"
#include "prototypes.h"
#include <string.h>


static int jacl_whitespace(int character);

void
eachturn()
{
	/* INCREMENT THE TOTAL NUMBER OF MOVES MADE AND CALL THE 'EACHTURN'
	 * FUNCTION FOR THE CURRENT LOCATION AND THE GLOBAL 'EACHTURN'
	 * FUNCTION. THESE FUNCTIONS CONTAIN ANY CODE THAT SIMULATED EVENTS
	 * OCCURING DUE TO THE PASSING OF TIME */
	TOTAL_MOVES->value++;
	execute("+eachturn");
	strcpy(function_name, "eachturn_");
	strcat(function_name, object[HERE]->label);
	execute(function_name);
	execute("+system_eachturn");

#ifndef GLK
	/* Runtime guarantee for web/CGI: re-emit the status bar at the
	 * end of every turn so the latest status_window_width (which the
	 * frontend reports on every AJAX request via &status_cols=) is
	 * reflected. verbs.library has done this since commit 248b2e9
	 * (2026-05-04) via `if interpreter = CGI: updatestatus` inside
	 * +system_eachturn, but a game running against an older library
	 * copy -- or a game that overrides +system_eachturn -- would
	 * otherwise keep an outdated bar. Re-emitting twice in the same
	 * response is harmless: the JS extracts whichever <jacl-status>
	 * marker arrives last and patches it into #statuswin. */
	web_render_status_bar();
#endif

	/* SET TIME TO FALSE SO THAT NO MORE eachturn FUNCTIONS ARE EXECUTED
	 * UNTIL EITHER THE COMMAND PROMPT IS RETURNED TO (AND TIME IS SET
	 * TO TRUE AGAIN, OR IT IS MANUALLY SET TO TRUE BY A VERB THAT CALLS
	 * MORE THAN ONE proxy COMMAND. THIS IS BECAUSE OTHERWISE A VERB THAT
	 * USES A proxy COMMAND TO TRANSLATE THE VERB IS RESULT IN TWO MOVES
	 * (OR MORE) HAVING PASSED FOR THE ONE ACTION. */
	TIME->value = FALSE;
}

int
get_here()
{
	/* THIS FUNCTION RETURNS THE VALUE OF 'here' IN A SAFE, ERROR CHECKED
	 * WAY */
	if (player < 1 || player > objects) {
		badplrrun(player);
		terminate(44);
	} else if (object[player]->PARENT < 1 || object[player]->PARENT> objects || object[player]->PARENT== player) {
		badparrun();
		terminate(44);
	} else {
		return (object[player]->PARENT);
	}

	/* SHOULDN'T GET HERE, JUST TRYING TO KEEP VisualC++ HAPPY */
	return 1;
}

char *
strip_return (char *string)
{
	/* STRIP ANY TRAILING RETURN OR NEWLINE OFF THE END OF A STRING */
	int index;
	int length = strlen(string);

	for (index = 0; index < length; index++) {
		switch (string[index]) {
		case '\r':
		case '\n':
			string[index] = 0;
			break;
		}
	}

	return string;
}

int
random_number()
{
	/* GENERATE A RANDOM NUMBER BETWEEN 0 AND THE CURRENT VALUE OF
	 * THE JACL VARIABLE MAX_RAND */

	rand();
	return (1 + (int) ((float) MAX_RAND->value * rand() / (RAND_MAX + 1.0)));
}

int
secure_random_number()
{
	/* LIKE random_number() BUT DRAWN FROM THE OPERATING SYSTEM'S
	 * CRYPTOGRAPHIC SOURCE, FOR VALUES A PLAYER MUST NOT BE ABLE TO
	 * PREDICT (ONE-TIME TOKENS, INVITE CODES). rand() IS SEEDED FROM THE
	 * CLOCK AND ITS OUTPUTS GIVE AWAY ITS STATE. FALLS BACK TO
	 * random_number() WHERE NO SUCH SOURCE IS AVAILABLE. */

	unsigned int	r;
	int				max = MAX_RAND->value;

	if (max < 1) return (random_number());
#if defined(_WIN32)
	if (rand_s(&r) != 0) return (random_number());
#elif defined(__NDS__)
	return (random_number());
#else
	{
		FILE *urandom = fopen("/dev/urandom", "rb");
		size_t got;

		if (urandom == NULL) return (random_number());
		got = fread(&r, sizeof(r), 1, urandom);
		fclose(urandom);
		if (got != 1) return (random_number());
	}
#endif
	return (1 + (int) (r % (unsigned int) max));
}

void
create_paths(char *full_path)
{
	int				index;
	char           *last_slash;

	/* SAVE A COPY OF THE SUPPLIED GAMEFILE NAME */
	strcpy(game_file, full_path);

	/* FIND THE LAST SLASH IN THE SPECIFIED GAME PATH AND REMOVE THE GAME
	 * FILE SUFFIX IF ANY EXISTS */
	last_slash = (char *) NULL;
	
	/* GET A POINTER TO THE LAST SLASH IN THE FULL PATH */
	last_slash = strrchr(full_path, DIR_SEPARATOR);

	/* Walk back from the last real character (not the NUL); the prior
	 * `index = strlen(full_path)` started one past the end and on the
	 * very first iteration read the NUL byte itself. Harmless because
	 * NUL is neither '.' nor a separator, but a true out-of-bounds
	 * read by one. */
	for (index = (int) strlen(full_path) - 1; index >= 0; index--) {
		if (full_path[index] == DIR_SEPARATOR){
			/* NO '.' WAS FOUND BEFORE THE LAST SLASH WAS REACHED,
			 * THERE IS NO FILE EXTENSION */
			break;
		} else if (full_path[index] == '.') {
			full_path[index] = 0;
			break;
		}
	}

	/* STORE THE GAME PATH AND THE GAME FILENAME PARTS SEPARATELY */
	if (last_slash == (char *) NULL) {
		/* GAME MUST BE IN CURRENT DIRECTORY SO THERE WILL BE NO GAME PATH */
		strcpy(prefix, full_path);
		game_path[0] = 0;

		/* THIS ADDITION OF ./ TO THE FRONT OF THE GAMEFILE IF IT IS IN THE
		 * CURRENT DIRECTORY IS REQUIRED TO KEEP Gargoyle HAPPY.
		 * temp_buffer is 1024; game_file is 256. snprintf into the
		 * smaller of the two to avoid the legacy strcpy walking off
		 * the end of game_file. */
#ifdef __NDS__
		snprintf (temp_buffer, 256, "%c%s", DIR_SEPARATOR, game_file);
#else
		snprintf (temp_buffer, 256, ".%c%s", DIR_SEPARATOR, game_file);
#endif
		strncpy (game_file, temp_buffer, 255);
		game_file[255] = 0;
	} else {
		/* STORE THE DIRECTORY THE GAME FILE IS IN WITH THE TRAILING
		 * SLASH IF THERE IS ONE */
		last_slash++;
		strcpy(prefix, last_slash);
		*last_slash = '\0';
		strcpy(game_path, full_path);
	}

#ifdef GLK
	/* walkthru / bookmark / blorb are 81-84 bytes; prefix is 81.
	 * Without the snprintf cap, a max-length prefix plus ".walkthru"
	 * (9 bytes) would overflow. */
	snprintf(walkthru, 81, "%s.walkthru", prefix);
	snprintf(bookmark, 81, "%s.bookmark", prefix);
#ifdef GARGLK
	/* Gargoyle uses glkunix_stream_open_pathname to open the blorb,
	 * which doesn't necessarily live in the cwd, so include the
	 * full game_path. blorb is 81 bytes; game_path is 256, so this
	 * is the buffer most likely to truncate. snprintf returns the
	 * unbounded length, which we ignore -- truncation is the right
	 * response for a path that wouldn't have fit anyway. */
	snprintf(blorb, 81, "%s/%s.blorb", game_path, prefix);
#else
	snprintf(blorb, 81, "%s.blorb", prefix);
#endif
#endif

	/* SET DEFAULT FILE LOCATIONS IF NOT SET BY THE USER IN CONFIG.
	 * include / temp / data directories are 81 bytes; game_path is
	 * 256. The legacy strcpy + strcat would overrun include_directory
	 * any time game_path was longer than ~72 chars -- easy to hit
	 * with a deeper install layout. snprintf truncates instead. */
	if (include_directory[0] == 0) {
		snprintf(include_directory, 512, "%s%s", game_path, INCLUDE_DIR);
	}

	if (temp_directory[0] == 0) {
		snprintf(temp_directory, 512, "%s%s", game_path, TEMP_DIR);
	}

	if (data_directory[0] == 0) {
		snprintf(data_directory, 512, "%s%s", game_path, DATA_DIR);
	}
}

int
jacl_whitespace(int character)
{
	/* CHECK IF A CHARACTER IS CONSIDERED WHITE SPACE IN THE JACL LANGUAGE */
	switch (character) {
		case ':':
		case '\t':
		case ' ':
			return(TRUE);
		default:
			return(FALSE);
	}
}

char *
stripwhite (char *string)
{
    int i;

	/* STRIP WHITESPACE FROM THE START AND END OF STRING. */
	while (jacl_whitespace (*string)) string++;

	i = strlen (string) - 1;

	while (i >= 0 && ((jacl_whitespace (*(string+ i))) || *(string + i) == '\n' || *(string + i) == '\r')) i--;

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

/* XOR-with-0xFF obfuscation, NOT encryption. There is no key; anyone
 * who notices the .j2 file starts with a "#encrypted" marker can
 * recover the original source with a 5-line script. The historical
 * names jacl_encrypt / jacl_decrypt overstated this -- "obfuscate"
 * is honest. Both directions are the same transform because XOR is
 * self-inverse; the two functions are kept distinct only for caller
 * intent (jpp obfuscates on write; loader deobfuscates on read).
 *
 * Both stop at the first '\n' or '\r' so the line terminator is
 * preserved verbatim in the obfuscated stream -- the loader still
 * uses fgets-style line splits to walk the file. */
void
jacl_obfuscate(char *string)
{
	int index, length;

	length = strlen(string);

	for (index = 0; index < length; index++) {
		if (string[index] == '\n' || string[index] == '\r') {
			return;
		}
		string[index] = string[index] ^ 255;
	}
}

/* XOR is self-inverse so deobfuscate is just obfuscate again; kept
 * as a separate symbol so callers read intent-side, not transform-
 * side. */
void
jacl_deobfuscate(char *string)
{
	jacl_obfuscate(string);
}

