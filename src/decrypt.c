/* decrypt.c --- General utility functions
 * (C) 2008 Stuart Allen, distribute and use 
 * according to GNU GPL, see file COPYING for details.
 */

#include "jacl.h"
#include "language.h"
#include "types.h"
#include "prototypes.h"
#include <string.h>

static int jacl_whitespace(int character);

int
main(int argc, char *argv[])
{
     FILE*		file;
	char			text_buffer[1024];
     int			encrypted = 0;

	if (argc < 2) {
		fprintf(stderr, "usage: %s <encrypted-jacl-file>\n",
			argc > 0 ? argv[0] : "decrypt");
		return 1;
	}

	if ((file = fopen(argv[1], "r")) == NULL) {
		fprintf(stderr, "%s: can't open %s\n", argv[0], argv[1]);
		return 1;
	}

	/* Drive the loop off fgets so a read error / EOF doesn't leave
	 * the last line in text_buffer to be re-printed. */
	while (fgets(text_buffer, 1024, file) != NULL) {
		if (strstr(text_buffer, "#encrypted")) {
			encrypted = 1;
			continue;
		}
		if (encrypted) {
			jacl_decrypt(text_buffer);
		}
		stripwhite(text_buffer);
		printf("%s", text_buffer);
	}

	fclose(file);
	return 0;
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

void
jacl_encrypt(char *string)
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

void
jacl_decrypt(char *string)
{
	int index, length;

	length = strlen(string);
	
	for (index = 0; index < length; index++) {
		if (string[index] == '\n' || string[index] == '\r') {
			string[index] = 0;
			return;
		}
		string[index] = string[index] ^ 255;
	}
}

