/* glk_saver.c --- Functions to save and load the game state to disk *
   using the GLK API.  (C) 2008 Stuart Allen, distribute and use * 
   according to GNU GPL, see file COPYING for details.  */ 

#include "jacl.h" 
#include "language.h"
#include "types.h"
#include "prototypes.h"


static void write_integer(FILE *file, int x);
static void write_long(FILE *file, long x);
static int  read_integer(FILE *file);
static long read_long(FILE *file);

int
save_game(const char *filename)
{
	struct integer_type *current_integer = integer_table;
    struct function_type *current_function = function_table;
    struct string_type *current_string = string_table;

	int             index, counter;
	FILE			*bookmark = NULL;
		
    if ((bookmark = fopen(filename, "wb")) == NULL) {
        return (FALSE);
    }

	/* THIS IS WRITTEN TO HELP VALIDATE THE SAVED GAME 
	 * BEFORE CONTINUING TO LOAD IT */
	write_integer (bookmark, objects);
	write_integer (bookmark, integers);
	write_integer (bookmark, functions);
	write_integer (bookmark, strings);

	while (current_integer != NULL) {
		write_integer (bookmark, current_integer->value);
		current_integer = current_integer->next_integer;
	}

	
    while (current_function != NULL) {
		if (current_function->nosave == FALSE) {
			write_integer (bookmark, current_function->call_count);
		}
		current_function = current_function->next_function;
    }

	for (index = 1; index <= objects; index++) {
		if (object[index]->nosave)
			continue;
		
		for (counter = 0; counter < 16; counter++) {
			write_integer (bookmark, object[index]->integer[counter]);
		}

		write_long (bookmark, object[index]->attributes);
		write_long (bookmark, object[index]->user_attributes);
	}

    while (current_string != NULL) {
        for (index = 0; index < 1024; index++) {
    	     putc(current_string->value[index], bookmark);
        }
        current_string = current_string->next_string;
    }

    for (index = 0; index < 1024; index++) {
        putc(last_command[index], bookmark);
    }

	write_integer (bookmark, player);
	write_integer (bookmark, noun[3]);

	/* CLOSE THE STREAM */
    fclose(bookmark);

	TIME->value = FALSE;
	return (TRUE);
}

int
restore_game(const char *filename, int warn)
{
	struct integer_type *current_integer = integer_table;
    struct function_type *current_function = function_table;
    struct string_type *current_string = string_table;

	int             index, counter;
	int             file_objects,
	                file_integers,
					file_functions,
					file_strings;
	FILE			*bookmark = NULL;
		
    if ((bookmark = fopen(filename, "rb")) == NULL) {
        return (FALSE);
    }

	/* THIS IS READ FIRST TO HELP VALIDATE THE SAVED GAME 
	 * BEFORE CONTINUING TO LOAD IT */
	file_objects = read_integer(bookmark);
	file_integers = read_integer(bookmark);
	file_functions = read_integer(bookmark);
	file_strings = read_integer(bookmark);

	if (file_objects != objects 
		|| file_integers != integers 
		|| file_functions != functions 
		|| file_strings != strings) {
		if (warn == FALSE) {
			log_error(cstring_resolve("BAD_SAVED_GAME")->value, PLUS_STDOUT);
		}

		fclose(bookmark);

#ifdef GLK
		TIME->value = FALSE;
		return (FALSE);
#else
		// THE SAVE FILE BEING LOADED IS INCOMPATIBLE SO START AGAIN
		// FROM THE BEGINNING AS THIS IS A RETURNING CGI USER.
		return (FALSE);
#endif
	}

	while (current_integer != NULL) {
		current_integer->value = read_integer(bookmark);
		current_integer = current_integer->next_integer;
	}

	while (current_function != NULL) {
		if (current_function->nosave == FALSE) {
			current_function->call_count = read_integer (bookmark);
		}

		current_function = current_function->next_function;
	}

	for (index = 1; index <= objects; index++) {
		if (object[index]->nosave) {
			continue;
		}

		for (counter = 0; counter < 16; counter++) {
			object[index]->integer[counter] = read_integer(bookmark);
		}

		object[index]->attributes = read_integer(bookmark);
		object[index]->user_attributes = read_integer(bookmark);
	}

    while (current_string != NULL) {
        for (index = 0; index < 1024; index++) {
            current_string->value[index] = getc(bookmark);
        }
        current_string = current_string->next_string;
    }

    for (index = 0; index < 1024; index++) {
        last_command[index] = getc(bookmark);
    }

	player = read_integer(bookmark);
	noun[3] = read_integer(bookmark);

	/* CLOSE THE STREAM */
	fclose (bookmark);

	TIME->value = FALSE;
	return (TRUE);
}

void
write_integer(FILE *file, int x)
{
	unsigned char c;

    c = (unsigned char) (x) & 0xFF;
    putc(c, file);
    c = (unsigned char) (x >> 8) & 0xFF;
    putc(c, file);
    c = (unsigned char) (x >> 16) & 0xFF;
    putc(c, file);
    c = (unsigned char) (x >> 24) & 0xFF;
    putc(c, file);
}

int 
read_integer(FILE *file)
{
    int a, b, c, d;
    a = (int) getc(file);
    b = (int) getc(file);
    c = (int) getc(file);
    d = (int) getc(file);
    return a | (b << 8) | (c << 16) | (d << 24);
}

void
write_long(FILE *file, long x)
{
	unsigned char c;

    c = (unsigned char) (x) & 0xFF;
    putc(c, file);
    c = (unsigned char) (x >> 8) & 0xFF;
    putc(c, file);
    c = (unsigned char) (x >> 16) & 0xFF;
    putc(c, file);
    c = (unsigned char) (x >> 24) & 0xFF;
    putc(c, file);
}

long 
read_long(FILE *file)
{
    long a, b, c, d;
    a = (long) getc(file);
    b = (long) getc(file);
    c = (long) getc(file);
    d = (long) getc(file);
    return a | (b << 8) | (c << 16) | (d << 24);
}
