/* dsjacl.c --- Nintendo DS JACL Interpreter
   (C) 1992-2008 Stuart Allen

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 1, or (at your option)
    any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "jacl.h"
#include "language.h"
#include "types.h"
#include "prototypes.h"

// EXTRA PROTOTYPES FOR DS
void jflush();
void output_text();
void print_text();
void write_right();
void write_centred();
void clrscrn();
void more();
void sleep();
void reset_styles();
void status_line();
void keyboard_status();
void read_keyboard();
void chordal_keyboard();
char chordal_character();
char caps_chordal_character();
char chordal_number();
char* touch_screen();
char* get_ds_string();

extern char			text_buffer[];
extern char			*word[];
extern int			quoted[];
extern int			punctuated[];
extern int			wp;

extern int			custom_error;
extern int			bad_command;
extern int			interrupted;

extern int			it;
extern int			them[];
extern int			her;
extern int			him;

extern int			oops_word;

extern int			encrypt;
extern int			encrypted;

extern int			notify;
extern int			debug;

char            include_directory[84] = "\0";
char            temp_directory[84] = "\0";
char            command_prompt[84] = "\0";
char            file_prompt[5] = ": \0";
char            margin_string[84] = "\0";
char            bookmark[84] = "\0";
char            walkthru[84] = "\0";
char			clicked_word[34] = "\0";
char            function_name[84];

char            default_function[84];
char            override[84];

char            temp_buffer[1024];
char            error_buffer[1024];
char 			chunk_buffer[4096];
char            proxy_buffer[1024];

char			oops_buffer[1024];
char			oopsed_current[1024];
char            last_command[1024];
char			*blank_command = "blankjacl\0";
char            *current_command = (char *) NULL;
char			command_buffer[1024];

int				walkthru_running = FALSE;
int				keyboard_mode = FALSE;

int				start_of_last_command;
int				start_of_this_command;

int             buffer_index = 0;
int             objects,
				variables;

FILE           *file = NULL;
FILE		   *transcript = NULL;
FILE		   *walkthru_file = NULL;

int             noun[4];
int             player = 0;

int             noun3_backup;
int             player_backup = 0;
int             screen_width = 32;
int             screen_depth = 23;
int				margin = 1;
int             row = 0;
int             column = 0;
int				updating_top_screen = FALSE;

int				bold_mode = 0;
int				reverse_mode = 0;
int				pre_mode = 0;
int				input_mode = 0;
int				subheader_mode = 0;
int				note_mode = 0;

int             variable_contents;
int             oec;
int            *object_element_address,
			   *object_backup_address;

int 			spaced = TRUE;

int 			noansi = FALSE;

int 			statusline = TRUE;

int       		delay = 0;

char            prefix[84] = "\0";
char            game_path[256] = "\0";
char            game_file[256] = "\0";

int             objects, integers, functions, strings;

struct object_type *object[MAX_OBJECTS];
struct integer_type *integer_table = NULL;
struct cinteger_type *cinteger_table = NULL;
struct window_type *window_table = NULL;
struct attribute_type *attribute_table = NULL;
struct string_type *string_table = NULL;
struct string_type *cstring_table = NULL;
struct function_type *function_table = NULL;
struct function_type *executing_function = NULL;
struct command_type *completion_list = NULL;
struct word_type *grammar_table = NULL;
struct synonym_type *synonym_table = NULL;
struct filter_type *filter_table = NULL;

PrintConsole topScreen;
PrintConsole bottomScreen;
//Keyboard	kbd;

int
main(argc, argv)
	 int             argc;
	 char           *argv[];
{
	int             index;

#ifdef ARM9
	defaultExceptionHandler();
#endif

	int fatrc = fatInitDefault();

    videoSetMode(MODE_0_2D);
    videoSetModeSub(MODE_0_2D);

    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankC(VRAM_C_SUB_BG);

    consoleInit(&topScreen, 3, BgType_Text4bpp, BgSize_T_256x256, 31, 0, true, true);
    consoleInit(&bottomScreen, 3, BgType_Text4bpp, BgSize_T_256x256, 31, 0, false, true);

	keysSetRepeat(30, 10);

	//keyboardInit(&kbd, 2, BgType_Text4bpp, BgSize_T_256x256, 31, 2, false, true);
    //keyboardShow();

	consoleSelect(&bottomScreen);

	printf("\x1b[37;0m"); 			// SET FONT TO DIM WHITE
	srand((int) time(NULL));

	override[0] = 0;

	// CREATE THE CONVENIENCE MARGIN STRING AND THE COMMAND PROMPT STRING
	for (index = 0; index < margin; index++) {
		margin_string[index] = ' ';
		command_prompt[index] = ' ';
	}
	margin_string[index] = 0;

	command_prompt[index++] = '>';
	command_prompt[index++] = ' ';
	command_prompt[index] = 0;

	clrscrn();
	write_text("^");
	version_info();
	write_text("you are welcome to redistribute it under certain ");
	write_text("conditions, type ~info~ at the game prompt for details.^");

	if (fatrc == 0) {
		write_text("Unable to initialise FAT filesystem, can't continue.^");
		terminate(1);
	}

	while (1) {
		printf("\x1b[33;1m"); 	// SET TO BRIGHT YELLOW
		subheader_mode = TRUE;
		write_text("^Type the name of the game file you wish to load.^");
		printf("\x1b[37;0m"); 			// SET FONT TO DIM WHITE
		subheader_mode = FALSE;
		write_text(": ");
		jflush();
		printf("\x1b[32;0m"); 							// SET TO DIM GREEN
		chordal_keyboard (temp_buffer, 1023);
		printf("\x1b[37;0m"); 							// SET TO DIM WHITE

		row = 0;
		newline();

		stripwhite(*(&temp_buffer));
	
		// TEMP_BUFFER NOW STORES THE NAME OF THE GAME TO BE OPENED
	
		for (index = 0; index < strlen(temp_buffer); index++) {
			if (temp_buffer[index] == 13 || temp_buffer[index] == 10 || temp_buffer[index] == ' ') {
				temp_buffer[index] = 0;
				break;
			}
		}
	
		// THIS CODE CONVERTS ALL BACK SLASHES TO FORWARD SLASHES AND IS
		// REQUIRED WHEN COMPILING FOR MS WINDOWS USING CYGWIN
	
		for (index = 0; index < strlen(temp_buffer); index++) {
			if (temp_buffer[index] == '\\')
				temp_buffer[index] = '/';
		}
	
		// SETUP ALL THE EXPECTED PATHS
		create_paths(temp_buffer);
	
		// ...AND OPEN THE RESULTING FILE AS OUR INPUT
		if ((file = fopen(game_file, "r")) == NULL) {
			// GAME FILE COULDN'T BE FOUND, TRY AGAIN WITH
			// .j2 APPENDED ONTO THE END
			strcat (game_file, ".j2");

			if ((file = fopen(game_file, "r")) == NULL) {
				sprintf(error_buffer, CANT_OPEN_SOURCE, game_file);
				log_error(error_buffer, ONLY_STDERR);
			} else {
				break;
			}
		} else {
			break;
		}
	}

	write_text("Loading...");
	jflush();

	read_gamefile();

	clrscrn();
	execute("+intro");

	if (object[2] == NULL) {
		write_text("ERROR: A JACL game must contain at least one object ");
		write_text("to represent the player, and at least one location ");
		write_text("for the player to start in.^");
		terminate(43);
	}

    // DUMMY RETRIEVE OF 'HERE' FOR TESTING OF GAME STATE
    get_here();

	eachturn();

	// TOP OF COMMAND LOOP
	do {
		custom_error = FALSE;

		execute("+bottom");

		jflush();

		newline();

		status_line();

		if (current_command != NULL) {
			strncpy(last_command, current_command, 256);
		}

		row = 0;
		printf("\x1b[37;0m"); 							// SET TO DIM WHITE
		printf("%s", command_prompt);
		printf("\x1b[32;0m"); 							// SET TO DIM GREEN
		chordal_keyboard (command_buffer, 1023);
		printf("\x1b[37;0m"); 							// SET TO DIM WHITE
		current_command = command_buffer;

		execute("+top");

		index = 0;

		if (*current_command) {
			while (*(current_command + index) && index < 1024) {
				if (*(current_command + index) == '\r' || *(current_command + index) == '\n') {
					break;
				} else {
					*(text_buffer + index) = tolower((int) *(current_command + index));
					index++;
				}
			}
		}

		text_buffer[index] = 0;

		if (text_buffer[0] == 0) {
			// NO COMMAND WAS SPECIFIED, FILL THE COMMAND IN AS 'blankjacl'
			// FOR THE GAME TO PROCESS AS DESIRED
			strcpy(text_buffer, "blankjacl");
			current_command = blank_command;
		}

		if (text_buffer[0] == '*') {
			if (transcript != NULL) {
				sprintf(temp_buffer, "NOTE: %s^", current_command + 1);
				write_text (temp_buffer);
				continue;
			} else {
				write_text ("No transcript running.^");
				continue;
			}
		} else {
			if (transcript != NULL) {
				fprintf(transcript, ">%s\n", text_buffer);
			}
		}

		command_encapsulate();
		jacl_truncate();
		
		index = 0;

		// SET THE INTEGER INTERRUPTED TO FALSE. IF THIS IS SET TO
		// TRUE BY ANY COMMAND, FURTHER PROCESSING WILL STOP
		INTERRUPTED->value = FALSE;

		interrupted = FALSE;

        if (word[0] != NULL) {
			if (strcmp(word[0], "undo")) {
				// COMMAND DOES NOT EQUAL undo
				save_game_state();
			}

            // COMMAND IS NOT NULL, START PROCESSING IT
			preparse();
        } else {
			// NO COMMAND WAS SPECIFIED, FILL THE COMMAND IN AS 'blankjacl'
			// FOR THE GAME TO PROCESS AS DESIRED
			strcpy(text_buffer, "blankjacl");
			command_encapsulate();
			preparse();
		}

	}
	while (TRUE);
}

void
preparse()
{
	int position;

	// THE INTERRUPTED VARIABLE IS USED TO STOP LATER ACTIONS IN A COMMAND 
    // IF ANY ONE
	while (word[wp] != NULL && INTERRUPTED->value == FALSE) {
		//printf("--- preparse %s\n", word[wp]);
		// PROCESS THE CURRENT COMMAND
		// CREATE THE command STRINGS FROM THIS POINT ONWARDS SO THE VERB OF
		// THE CURRENT COMMAND IS ALWAYS command[0]. 

		//clear_cstring("command");

		position = wp;

		/*
		while (word[position] != NULL && strcmp(word[position], cstring_resolve("THEN_WORD")->value)) {
			add_cstring ("command", word[position]);
			position++;
		};
		*/

		// PROCESS THE COMMAND
		word_check();

		/* THE PREVIOUS COMMAND HAS FINISHED, LOOK FOR ANOTHER COMMAND */
		while (word[wp] != NULL) {
			if (word[wp] != NULL && !strcmp(word[wp], cstring_resolve("THEN_WORD")->value)) {
				wp++;
				break;
			}
			wp++;
		}
	}
}

void
word_check()
{
	int index;

	/* REMEMBER THE START OF THIS COMMAND TO SUPPORT 'oops' AND 'again' */
	start_of_this_command = wp;
	//printf("--- command starts at %d\n", start_of_this_command);

	/* START CHECKING THE PLAYER'S COMMAND FOR SYSTEM COMMANDS */
	if (!strcmp(word[wp], cstring_resolve("QUIT_WORD")->value) || !strcmp(word[wp], "q")) {
		if (execute("+quit_game") == FALSE) {
			TIME->value = FALSE;
			write_text(cstring_resolve("SURE_QUIT")->value);
			if (get_yes_or_no()) {
				newline();
				execute("+score");
				terminate(0);
			} else {
				write_text(cstring_resolve("RETURN_GAME")->value);
			}
		}
	} else if (!strcmp(word[wp], cstring_resolve("RESTART_WORD")->value)) {
		if (execute("+restart_game") == FALSE) {
			TIME->value = FALSE;
			write_text(cstring_resolve("SURE_RESTART")->value);
			if (get_yes_or_no()) {
				write_text(cstring_resolve("RESTARTING")->value);
				restart_game();
				clrscrn();
				execute ("+intro");
				eachturn();
			} else {
				write_text(cstring_resolve("RETURN_GAME")->value);
			}
		}
	} else if (!strcmp(word[wp], cstring_resolve("UNDO_WORD")->value)) {
		if (execute("+undo_move") == FALSE) {
			undoing();
		}
	} else if (!strcmp(word[wp], cstring_resolve("OOPS_WORD")->value) || !strcmp(word[wp], "o")) {
		//printf("--- oops word is %d\n", oops_word);
		if (word[++wp] != NULL) {
			if (oops_word == -1) {
				if (TOTAL_MOVES->value == 0) {
					write_text(cstring_resolve("NO_MOVES")->value);
					TIME->value = FALSE;
				} else {
					write_text(cstring_resolve("CANT_CORRECT")->value);
					TIME->value = FALSE;
				}
			} else {
				strncpy(oops_buffer, word[wp], 255);
				strncpy(text_buffer, last_command, 255);
				command_encapsulate();
				//printf("--- trying to replace %s with %s\n", word[oops_word], oops_buffer);
				jacl_truncate();
				word[oops_word] = (char *) &oops_buffer;

				/* BUILD A PLAIN STRING REPRESENTING THE NEW COMMAND */
				oopsed_current[0] = 0;
				index = 0;

				while (word[index] != NULL) {
					if (oopsed_current[0] != 0) {
						strcat(oopsed_current, " ");
					}

					strcat(oopsed_current, word[index]);

					index++;
				}

				current_command = oopsed_current;
				//printf("--- current command is: %s\n", current_command);

				/* PROCESS THE FIXED COMMAND ONLY */
				wp = start_of_last_command;
				word_check();
			}
		} else {
			write_text(cstring_resolve("BAD_OOPS")->value);
			TIME->value = FALSE;
		}
	} else if (!strcmp(word[wp], cstring_resolve("AGAIN_WORD")->value) || !strcmp(word[wp], "g")) {
		if (TOTAL_MOVES->value == 0) {
			write_text(cstring_resolve("NO_MOVES")->value);
			TIME->value = FALSE;
		} else if (last_command[0] == 0) {
			write_text(cstring_resolve("NOT_CLEVER")->value);
			TIME->value = FALSE;
		} else {
			strncpy(text_buffer, last_command, 256);
			current_command = last_command;
			command_encapsulate();
			jacl_truncate();
			//printf("--- command started at %d\n", start_of_last_command);
			wp = start_of_last_command;
			word_check();
		}
	} else if (!strcmp(word[wp], cstring_resolve("INFO_WORD")->value) || !strcmp(word[wp], "version")) {
		version_info();
		write_text("you can redistribute it and/or modify it under the ");
		write_text("terms of the GNU General Public License as published by ");
		write_text("the Free Software Foundation; either version 2 of the ");
		write_text("License, or any later version.^^");
		write_text("This program is distributed in the hope that it will be ");
		write_text("useful, but WITHOUT ANY WARRANTY; without even the ");
		write_text("implied warranty of MERCHANTABILITY or FITNESS FOR A ");
		write_text("PARTICULAR PURPOSE. See the GNU General Public License ");
		write_text("for more details.^^");
		write_text("You should have received a copy of the GNU General ");
		write_text("Public License along with this program; if not, write ");
		write_text("to the Free Software Foundation, Inc., 675 Mass Ave, ");
		write_text("Cambridge, MA 02139, USA.^^");
		sprintf(temp_buffer, "OBJECTS DEFINED:   %d^", objects);
		write_text(temp_buffer);
		TIME->value = FALSE;
	} else {
		/* NO WORD HAS BEEN MARKED AS AN ERROR YET*/
		oops_word = -1;

		/* THIS IS NOT A SYSTEM COMMAND, CALL parser TO PROCESS THE COMMAND */
		parser();
	}

	start_of_last_command = start_of_this_command;
}

void
version_info()
{
	char            buffer[80];

	printf("\x1b[33;1m"); 	// SET TO BRIGHT YELLOW
	subheader_mode = TRUE;
	sprintf(buffer, "JACL Interpreter v%d.%d.%d^", J_VERSION, J_RELEASE,
			J_BUILD);
	write_text(buffer);
	printf("\x1b[37;0m"); 			// SET FONT TO DIM WHITE
	subheader_mode = FALSE;
	write_text("Copyright (c) 1992-2010^Stuart Allen.^^");
	write_text("This program is free software; ");
}

void
write_text(tout_buffer)
	 char            *tout_buffer;
{
	int             index;

	if (!strcmp(tout_buffer, "tilde")) {
		chunk_buffer[buffer_index] = '~';
		buffer_index++;
		chunk_buffer[buffer_index] = 0;
		return;
	} else if (!strcmp(tout_buffer, "circumflex")) {
		chunk_buffer[buffer_index] = '^';
		buffer_index++;
		chunk_buffer[buffer_index] = 0;
		return;
	} else if (!strcmp(tout_buffer, "lessthan")) {
		chunk_buffer[buffer_index] = '<';
		buffer_index++;
		chunk_buffer[buffer_index] = 0;
		return;
	} else if (!strcmp(tout_buffer, "greaterthan")) {
		chunk_buffer[buffer_index] = '>';
		buffer_index++;
		chunk_buffer[buffer_index] = 0;
		return;
	}

	for (index = 0; index < strlen(tout_buffer); index++) {
		if (tout_buffer[index] == '^') {
			/* NULL TERMINATE CHUNK_BUFFER */
			chunk_buffer[buffer_index] = 0;

			/* WRITE OUT THE CURRENT CONTENTS OF CHUNK_BUFFER 
			 * AND PERFORM A NEWLINE() */
			output_text(chunk_buffer, TRUE);

			/* CLEAR CONTENTS OF CHUNK_BUFFER */
			chunk_buffer[0] = 0;
		} else if (tout_buffer[index] == '~') {
			chunk_buffer[buffer_index] = ('\"');
			buffer_index++;
		} else {
			chunk_buffer[buffer_index] = tout_buffer[index];
			buffer_index++;
		}
	}

	chunk_buffer[buffer_index] = 0;
}

void
jflush()
{
	if (buffer_index != 0) {
   		output_text(chunk_buffer, FALSE);	
		/* WRITE OUT THE CURRENT CONTENTS OF CHUNK_BUFFER AND WITHOUT A 
		 * NEWLINE() */
		}

	fflush (stdout);			
}

void
output_text(write_buffer, print_newline)
	 char           *write_buffer;
	 int			print_newline;
{
	int             index;
	char           *pointer;

	/* THIS METHOD ACCEPTS A STRING A WRITES IT TO THE SCREEN ONE LINE
	 * AT A TIME, WRAPPING AT WORDS SEPARATED BY SPACES. IT GETS THE
	 * CURRENT SCREEN WIDTH FROM THE VARIABLE screen_width, THE CURRENT
     * POSITION OF THE CURSOR FROM THE VARIABLE column AND THE CURRENT
	 * WIDTH OF THE SIDE MARGINS FROM THE VARIABLE margin */

	if (strlen(write_buffer) + column <= (screen_width - (2 * margin))) {
		/* IF THE ENTIRE STRING FITS ON THE REMAINER OF THE LINE, PRINT IT */
		if (*write_buffer != 0 ) {
			print_text(write_buffer);
		}

		if (print_newline) {
			newline();
		}

		/* CLEAR THE POINTER INDICATING WHERE chunk_buffer IS UP TO
		 * WHEN ADDING MORE TEXT TO IT. ie START ADDING AT THE
		 * BEGINNING NOW THAT THE CONTENTS HAVE BEEN DUMPED. */
		buffer_index = 0;
		chunk_buffer[0] = 0;
		
		return;
	} else {
		/* THE STRING DOESN'T FIT IN ITS ENTIRITY, SO LOOK FOR A SPACE
		 * THAT IS JUST BEFORE THE SCREEN EDIT TO SPLIT THE STRING ON */
		for (index = screen_width - column - (margin * 2); index > 0; index--) {
			/* WORK BACKWARDS FROM ONE SCREEN WIDTH */
			if (*(write_buffer + index) == 32) {
				/* SET THE FIRST SPACE TO 0 */
				*(write_buffer + index) = 0;

				/* PRINT THAT LINES WORTH THEN PROCESS THE REMAINER */
				print_text(write_buffer);
				newline();

				/* CALL output_text AGAIN TO PRINT THE NEXT LINE */
				index++;
				pointer = (write_buffer + index);
				output_text(pointer, print_newline);
				/* RETURN FROM ALL THE RECURSIVE CALLS */
				return;	
			}
		}

		/* CHUNK_BUFFER BEGINS WITH A SINGLE WORD THAT IS TOO BIG 
		 * TO FIT ON THE SCREEN, JUST PRINT IT */

		if (column != 0) {
			/* WE ARE NOT IN THE FIRST COLUMN (THIS CAN ONLY HAPPEN
			 * WHEN USING jflush FUNCTION) SO, DO A newline THEN
			 * TRY AGAIN WITH A WHOLE LINE TO USE */
			newline();
			output_text(write_buffer, print_newline);
			return;
		}

		index = screen_width - column - (margin * 2);

		/* USE A COPY AS THERE IS NO SPACE IN WHICH TO FIT THE 
		 * NULL TERMINATION OF THE STRING */
		strncpy(temp_buffer, write_buffer, index);

		/* WRITE n BYTES TO ARRAY INDEXES 0 - (n-1), THEN PUT A NULL
		 * TERMINATION AT n */
		temp_buffer[index] = 0;

		print_text(&temp_buffer[0]);	
		newline();

		/* CALL output_text AGAIN TO PRINT THE NEXT LINE */
		pointer = (write_buffer + index);
		output_text(pointer, print_newline);
	}

 }

void 
print_text(print_buffer) 
	 char            *print_buffer;
{
	/* PRINT THE MARGIN IF THIS IS THE FIRST TEXT ON THIS LINE */
	if (margin && column == 0) printf("%s", margin_string);

	if (transcript != NULL) {
		/* THERE IS A TRANSCRIPT FILE OPEN, WRITE THIS TEXT THERE TOO */
		fprintf(transcript, "%s", print_buffer);
	}

	if (delay) {
		/* SIMULATE SLOW TERMINAL OUTPUT */
		while (*print_buffer) {
			putchar(*print_buffer);
			fflush (stdout);			
			sleep (delay);
			print_buffer++;
		}
	} else {
		printf("%s", (char *) print_buffer);	
	}

	// WAIT FOR THE VERTICAL BLANK INTERRUPT (SCREEN REFRESH)
	swiWaitForVBlank();

	/* UPDATE THE CURRENT CURSOR POSITION */
	column += strlen(print_buffer);
}


void 
sleep(unsigned int mseconds)
{
	/* WAIT FOR A GIVEN NUMBER OF MILLISECONDS */
    clock_t goal = mseconds + clock();
    while (goal > clock());
}

void
status_line()
{
	consoleSelect(&topScreen);
	updating_top_screen = TRUE;

    execute("+update_top_screen");

	if (keyboard_mode) {
		printf("\x1b[23;0HNUMPAD"); 	// CURSOR TO FIRST COLUMN
	} else {
		printf("\x1b[23;0H      "); 	// CURSOR TO FIRST COLUMN
	}

	updating_top_screen = FALSE;
	consoleSelect(&bottomScreen);
}

void
keyboard_status()
{
	consoleSelect(&topScreen);
	updating_top_screen = TRUE;

	if (keyboard_mode == 1) {
		printf("\x1b[23;0HCaps Lock"); 	// CURSOR TO FIRST COLUMN
	} else if (keyboard_mode == 2) {
		printf("\x1b[23;0HNum Lock "); 	// CURSOR TO FIRST COLUMN
	} else {
		printf("\x1b[23;0H         "); 	// CURSOR TO FIRST COLUMN
	}

	updating_top_screen = FALSE;
	consoleSelect(&bottomScreen);
}

void
newline()
{
	/* START A NEW LINE ON THE SCREEN */
	if (transcript != NULL) {
		fprintf(transcript, "\n");
	}

	printf("\n");
	column = 0;
	scroll();
}

void
scroll()
{
	// DON'T COUNT OUTPUT TO THE BOTTOM SCREEN AS A NEW LINE
	if (updating_top_screen) return;

	/* MOVE THE CURRENT LINE FORWARD ONE AND CHECK IF A [MORE] PROMPT IS 
	 * REQUIRED */
	row++;

	//printf("--- row: %d\n", row);

	if (row == screen_depth)
		more("[MORE]");
}

int
get_yes_or_no()
{
    char *cx;
	char commandbuf[256];

	while (1) {
		write_text("^Please enter ~yes~ or ~no~: ");
		jflush();

		printf("\x1b[32;0m"); 							// SET TO DIM GREEN
		chordal_keyboard (commandbuf, 1023);
		printf("\x1b[37;0m"); 							// SET TO DIM WHITE
		column = 0;
		row = 0;

        for (cx = commandbuf; *cx == ' '; cx++) { };

        if (*cx == 'y' || *cx == 'Y')
            return TRUE;
        if (*cx == 'n' || *cx == 'N')
            return FALSE;
            
	}
}

char
get_character(message)
  char			*message;
{
    char *cx;
	char commandbuf[256];

	write_text(message);
	jflush();
	printf("\x1b[32;0m"); 							// SET TO DIM GREEN
	chordal_keyboard (commandbuf, 1023);
	printf("\x1b[37;0m"); 							// SET TO DIM WHITE
	column = 0;
	row = 0;

    for (cx = commandbuf; *cx == ' '; cx++) { };

	return (*cx);
}

void
get_string(char *string_buffer)
{
    char *cx;
	char commandbuf[256];
	int index;

	jflush();
	printf("\x1b[32;0m"); 							// SET TO DIM GREEN
	chordal_keyboard (commandbuf, 1023);
	printf("\x1b[37;0m"); 							// SET TO DIM WHITE
	column = 0;
	row = 0;

    for (cx = commandbuf; *cx == ' '; cx++) { };

	for (index = 0; index < strlen(commandbuf); index++) {
		if (*(cx + index) == 13 || *(cx + index) == 10) {
			*(cx + index) = 0;
			break;
		}
	}

	// COPY UP TO 255 BYTES OF THE ENTERED TEXT INTO THE SUPPLIED STRING
	strncpy (string_buffer, cx, 255);
	
}

char *
get_ds_string(message)
  char			*message;
{
    char *cx;
	char commandbuf[256];
	int index;

	write_text(message);
	jflush();
	printf("\x1b[32;0m"); 							// SET TO DIM GREEN
	chordal_keyboard (commandbuf, 1023);
	printf("\x1b[37;0m"); 							// SET TO DIM WHITE
	column = 0;
	row = 0;

    for (cx = commandbuf; *cx == ' '; cx++) { };

	for (index = 0; index < strlen(commandbuf); index++) {
		if (*(cx + index) == 13 || *(cx + index) == 10 || *(cx + index) == ' ') {
			*(cx + index) = 0;
			break;
		}
	}

	return cx;
}

void
clrscrn()
{
	consoleClear();
	column = 0;
	row = 0;
}

int 
get_number(insist, low, high)
	int		insist;
	int		low;
	int		high;
{
    char *cx;
	char commandbuf[256];
	int response;
	int index = 0;
    
	sprintf(temp_buffer, TYPE_NUMBER, low, high);

    /* THIS LOOP IS IDENTICAL TO THE MAIN COMMAND LOOP IN glk_main(). */
    
    while (1) {
   		write_text(temp_buffer);
		jflush();

		printf("\x1b[32;0m"); 							// SET TO DIM GREEN
		chordal_keyboard (commandbuf, 1023);
		printf("\x1b[37;0m"); 							// SET TO DIM WHITE
		column = 0;
		row = 0;
		index = 0;

        for (cx = commandbuf; *cx == ' '; cx++) { };

		while (*(cx + index)) {
			if (*(cx + index) == '\r' || *(cx + index) == '\n'|| *(cx + index) == ' ') {
				*(cx + index) = 0;
				break;
			}

			index++;
		}	
        
		if (validate(cx)) {
			response = atoi(cx);
			if (response >= low && response <= high) {
				return (response);
			}
		}

		if (!insist) {
			return (-1);
		}
    }
}

void
more(message)
	char* message;
{
	int character;

	/* DON'T DISPLAY 'MORE' PROMPTS WHILE RUNNING A WALKTHRU */
	if (walkthru_running) return;

	printf("%s\x1b[32;1m", margin_string); 			// SET TO GREEN
	printf("%s", message);
	printf("\x1b[37;0m"); 							// SET TO WHITE

	character = get_key();

	printf("\x1b[%dD", strlen(message)); 	// CURSOR TO FIRST COLUMN
	printf("                              ");			// BLANK OUT [MORE]
	printf("\x1b[31D"); 					// CURSOR TO FIRST COLUMN

	reset_styles();

	column=0;
	row = 0;
}

int 
get_key()
{
	char character;

    while(1) {
       	swiWaitForVBlank();

		scanKeys();

        character = chordal_character();

        if(character > 0) {
       		swiWaitForVBlank();
			return (int) character;
		} 

    }
}

void
reset_styles()
{
	printf("\x1b[0m");
	if (bold_mode) printf("\x1b[1m");
	if (reverse_mode) printf("\x1b[7m");
	if (input_mode) printf("\x1b[32;0m");
	if (subheader_mode) printf("\x1b[33;1m");
	if (note_mode) printf("\x1b[34;1m");
}

void
save_game_state()
{
	/* THIS FUNCTION MAKES AN IN-MEMORY COPY OF THE GAME STATE AFTER EACH
	 * OF THE PLAYER'S COMMANDS SO THE 'undo' COMMAND CAN BE USED */
	int             index,
	                counter;

	struct integer_type *current_integer = integer_table;
	struct function_type *current_function = function_table;

	do {
		current_function->call_count_backup = current_function->call_count;
		current_function = current_function->next_function;
	}
	while (current_function != NULL);

	do {
		current_integer->value_backup = current_integer->value;
		current_integer = current_integer->next_integer;
	}
	while (current_integer != NULL);

	for (index = 1; index <= objects; index++) {
		if (object[index]->nosave)
			continue;

		for (counter = 0; counter < 16; counter++) {
			object[index]->integer_backup[counter] =
				object[index]->integer[counter];
		}

		object[index]->attributes_backup = object[index]->attributes;
		object[index]->user_attributes_backup = object[index]->user_attributes;
	}

	player_backup = player;
	noun3_backup = noun[3];
}

void
restore_game_state()
{
	/* THIS FUNCTION IS CALLED AS A RESULT OF THE PLAYER USING THE 'undo'
	 * COMMAND */
	int             index,
	                counter;

	struct integer_type *current_integer = integer_table;
	struct function_type *current_function = function_table;

	do {
		current_function->call_count = current_function->call_count_backup;
		current_function = current_function->next_function;
	}
	while (current_function != NULL);


	do {
		current_integer->value = current_integer->value_backup;
		current_integer = current_integer->next_integer;
	}
	while (current_integer != NULL);

	for (index = 1; index <= objects; index++) {
		if (object[index]->nosave)
			continue;

		for (counter = 0; counter < 16; counter++)
			object[index]->integer[counter] =
				object[index]->integer_backup[counter];

		object[index]->attributes = object[index]->attributes_backup;
		object[index]->user_attributes = object[index]->user_attributes_backup;
	}

	player = player_backup;
	noun[3] = noun3_backup;

	write_text(cstring_resolve("MOVE_UNDONE")->value);
	object[HERE]->attributes &= ~1;
	execute("+top");
	execute ("+look_around");
	execute("+bottom");
	TIME->value = FALSE;
}

int
restore_interaction(filename)
    char        *filename;
{

    if (filename == NULL) {
		filename = get_ds_string("Enter a filename: ");
    }

    if (restore_game(filename, TRUE) == FALSE) {
        write_text(cstring_resolve("CANT_RESTORE")->value);
        return (FALSE);
    } else {
        return (TRUE);
    }
}

int
save_interaction(filename)
    char        *filename;
{

    if (filename == NULL) {
		filename = get_ds_string("Enter a filename: ");
    }

    if (save_game(filename)) {
        return (TRUE);
    } else {
        write_text(cstring_resolve("CANT_SAVE")->value);
        return (FALSE);
    }
}

void
undoing()
{
	if (TOTAL_MOVES->value && strcmp(last_command, cstring_resolve("UNDO_WORD")->value)) {
		restore_game_state();
	} else {
		write_text(cstring_resolve("NO_UNDO")->value);
		TIME->value = FALSE;
	}
}

void
chordal_keyboard(char *input_buffer, int maxlen) {
	char character = ' ';
	char* touch_word = NULL;
	int index = 0;

	while(index < maxlen) {
		scanKeys();

		if(keysDown() & KEY_TOUCH) {
			touch_word = touch_screen();
			if (touch_word != NULL) {
				// MANUALLY INSERT A SPACE IF OTHER TEXT HAS ALREADY BEEN 
				// ADDED THE THE COMMAND AND IS NOT FOLLOWED BY A SPACE
				if (index != 0 && *(input_buffer + index - 1) != ' ') {
					putchar(' ');
					*(input_buffer + index++) = ' ';
				}

				// NOW INSERT THE SELECTED WORD INTO THE PLAYER'S COMMAND
				int counter;
				for (counter = 0; counter < strlen(touch_word); counter++) {
					putchar(tolower((int) *(touch_word + counter)));
					*(input_buffer + index++) = *(touch_word + counter);
				}

				// PUT A MANUAL SPACE AFTER THE WORD INSERTED
				putchar(' ');
				*(input_buffer + index++) = ' ';
			}
		} else if (keyboard_mode == 0 && (character = chordal_character()) != 0) {
			putchar(character);
			*(input_buffer + index++) = character;	
		} else if (keyboard_mode == 1 && (character = caps_chordal_character()) != 0) {
			putchar(character);
			*(input_buffer + index++) = character;	
		} else if (keyboard_mode == 2 && (character = chordal_number()) != 0) {
			putchar(character);
			*(input_buffer + index++) = character;	
 		} else if(keysDownRepeat() & KEY_A && keysHeld() & KEY_X && index != 0) {
			// DELETE THE PREVIOUS CHARACTER FROM THE BUFFER
			*(input_buffer + --index) = 0;
			printf("\x1b[1D \x1b[1D");
 		} else if(keysDown() & KEY_SELECT) {
			keyboard_mode++;
			if (keyboard_mode == 3) {
				keyboard_mode = 0;
			}
			keyboard_status();	
 		} else if((keysDown() & KEY_B || keysHeld() & KEY_B) && keysDown() & KEY_A) {
			// PRESSED RETURN
			printf("\n");
			break;
		}

		swiWaitForVBlank();
	}

	// NULL TERMINATE THE STRING
	*(input_buffer + index) = 0;
}

char
chordal_character()
{
	if(keysDown() & KEY_UP && keysHeld() & KEY_L && keysHeld() & KEY_R) {
		return('\"');
	} else if(keysDown() & KEY_DOWN && keysHeld() & KEY_L && keysHeld() & KEY_R) {
		return('.');
	} else if(keysDown() & KEY_LEFT && keysHeld() & KEY_L && keysHeld() & KEY_R) {
		return('\'');
	} else if(keysDown() & KEY_RIGHT && keysHeld() & KEY_L && keysHeld() & KEY_R) {
		return(',');
	} else if(keysDown() & KEY_UP && keysHeld() & KEY_A) {
		return('n');
	} else if(keysDown() & KEY_DOWN && keysHeld() & KEY_A) {
		return('s');
	} else if(keysDown() & KEY_LEFT && keysHeld() & KEY_A) {
		return('t');
	} else if(keysDown() & KEY_RIGHT && keysHeld() & KEY_A) {
		return('h');
	} else if(keysDown() & KEY_UP && keysHeld() & KEY_X) {
		return('c');
	} else if(keysDown() & KEY_DOWN && keysHeld() & KEY_X) {
		return('e');
	} else if(keysDown() & KEY_LEFT && keysHeld() & KEY_X) {
		return('l');
	} else if(keysDown() & KEY_RIGHT && keysHeld() & KEY_X) {
		return('r');
	} else if(keysDown() & KEY_UP && keysHeld() & KEY_B) {
		return('u');
	} else if(keysDown() & KEY_DOWN && keysHeld() & KEY_B) {
		return('d');
	} else if(keysDown() & KEY_LEFT && keysHeld() & KEY_B) {
		return('q');
	} else if(keysDown() & KEY_RIGHT && keysHeld() & KEY_B) {
		return('e');
	} else if(keysDown() & KEY_UP && keysHeld() & KEY_Y) {
		return('p');
	} else if(keysDown() & KEY_DOWN && keysHeld() & KEY_Y) {
		return('y');
	} else if(keysDown() & KEY_LEFT && keysHeld() & KEY_Y) {
		return('g');
	} else if(keysDown() & KEY_RIGHT && keysHeld() & KEY_Y) {
		return('b');
	} else if(keysDown() & KEY_UP && keysHeld() & KEY_R) {
		return('k');
	} else if(keysDown() & KEY_DOWN && keysHeld() & KEY_R) {
		return('f');
	} else if(keysDown() & KEY_LEFT && keysHeld() & KEY_R) {
		return('x');
	} else if(keysDown() & KEY_RIGHT && keysHeld() & KEY_R) {
		return('j');
	} else if(keysDown() & KEY_UP && keysHeld() & KEY_L) {
		return('m');
	} else if(keysDown() & KEY_DOWN && keysHeld() & KEY_L) {
		return('v');
	} else if(keysDown() & KEY_LEFT && keysHeld() & KEY_L) {
		return('w');
	} else if(keysDown() & KEY_RIGHT && keysHeld() & KEY_L) {
		return('z');
 	} else if(keysDown() & KEY_B && keysHeld() & KEY_Y) {
		return(' ');
	} else if(keysDown() & KEY_UP) {
		return('a');
	} else if(keysDown() & KEY_DOWN) {
		return('i');
	} else if(keysDown() & KEY_LEFT) {
		return('o');
	} else if(keysDown() & KEY_RIGHT) {
		return('e');
	}

	return ((char) 0);
}

char
caps_chordal_character()
{
	if(keysDown() & KEY_UP && keysHeld() & KEY_L && keysHeld() & KEY_R) {
		return('\"');
	} else if(keysDown() & KEY_DOWN && keysHeld() & KEY_L && keysHeld() & KEY_R) {
		return('.');
	} else if(keysDown() & KEY_LEFT && keysHeld() & KEY_L && keysHeld() & KEY_R) {
		return('\'');
	} else if(keysDown() & KEY_RIGHT && keysHeld() & KEY_L && keysHeld() & KEY_R) {
		return(',');
	} else if(keysDown() & KEY_UP && keysHeld() & KEY_A) {
		return('N');
	} else if(keysDown() & KEY_DOWN && keysHeld() & KEY_A) {
		return('S');
	} else if(keysDown() & KEY_LEFT && keysHeld() & KEY_A) {
		return('T');
	} else if(keysDown() & KEY_RIGHT && keysHeld() & KEY_A) {
		return('H');
	} else if(keysDown() & KEY_UP && keysHeld() & KEY_X) {
		return('C');
	} else if(keysDown() & KEY_DOWN && keysHeld() & KEY_X) {
		return('E');
	} else if(keysDown() & KEY_LEFT && keysHeld() & KEY_X) {
		return('L');
	} else if(keysDown() & KEY_RIGHT && keysHeld() & KEY_X) {
		return('R');
	} else if(keysDown() & KEY_UP && keysHeld() & KEY_B) {
		return('U');
	} else if(keysDown() & KEY_DOWN && keysHeld() & KEY_B) {
		return('D');
	} else if(keysDown() & KEY_LEFT && keysHeld() & KEY_B) {
		return('Q');
	} else if(keysDown() & KEY_RIGHT && keysHeld() & KEY_B) {
		return('E');
	} else if(keysDown() & KEY_UP && keysHeld() & KEY_Y) {
		return('P');
	} else if(keysDown() & KEY_DOWN && keysHeld() & KEY_Y) {
		return('Y');
	} else if(keysDown() & KEY_LEFT && keysHeld() & KEY_Y) {
		return('G');
	} else if(keysDown() & KEY_RIGHT && keysHeld() & KEY_Y) {
		return('B');
	} else if(keysDown() & KEY_UP && keysHeld() & KEY_R) {
		return('K');
	} else if(keysDown() & KEY_DOWN && keysHeld() & KEY_R) {
		return('F');
	} else if(keysDown() & KEY_LEFT && keysHeld() & KEY_R) {
		return('X');
	} else if(keysDown() & KEY_RIGHT && keysHeld() & KEY_R) {
		return('J');
	} else if(keysDown() & KEY_UP && keysHeld() & KEY_L) {
		return('M');
	} else if(keysDown() & KEY_DOWN && keysHeld() & KEY_L) {
		return('V');
	} else if(keysDown() & KEY_LEFT && keysHeld() & KEY_L) {
		return('W');
	} else if(keysDown() & KEY_RIGHT && keysHeld() & KEY_L) {
		return('Z');
 	} else if(keysDown() & KEY_B && keysHeld() & KEY_Y) {
		return(' ');
	} else if(keysDown() & KEY_UP) {
		return('A');
	} else if(keysDown() & KEY_DOWN) {
		return('I');
	} else if(keysDown() & KEY_LEFT) {
		return('O');
	} else if(keysDown() & KEY_RIGHT) {
		return('E');
	}

	return ((char) 0);
}

char
chordal_number()
{
	if(keysDown() & KEY_L) {
		return('0');
	} else if(keysDown() & KEY_R) {
		return('1');
	} else if(keysDown() & KEY_LEFT) {
		return('2');
	} else if(keysDown() & KEY_UP) {
		return('3');
	} else if(keysDown() & KEY_RIGHT) {
		return('4');
	} else if(keysDown() & KEY_DOWN) {
		return('5');
	} else if(keysDown() & KEY_Y) {
		return('6');
	} else if(keysDown() & KEY_X) {
		return('7');
	} else if(keysDown() & KEY_A) {
		return('8');
	} else if(keysDown() & KEY_B) {
		return('9');
	}

	return ((char) 0);
}

void
read_keyboard(char *input_buffer, int maxlen) {
	int index = 0;

	while(index < maxlen) {
        
		int key = keyboardUpdate();

		if(key > 0) {
			if (key == 8 && index != 0) {
				// DELETE THE PREVIOUS CHARACTER FROM THE BUFFER
				*(input_buffer + --index) = 0;
				printf("\x1b[1D \x1b[1D");
			} else if (key == 10) {
				printf("%c", key);
				break;
			} else if (key > 31 && key < 123) {
				// ADD THE KEY TO THE BUFFER AND DISPLAY IT
				*(input_buffer + index++) = key;
				printf("%c", key);
			}

			// WAIT FOR THE VERTICAL BLANK INTERRUPT (SCREEN REFRESH)
			swiWaitForVBlank();
		}
	}

	// NULL TERMINATE THE STRING
	*(input_buffer + index) = 0;
}

char *
touch_screen() {
	int row, column, pointer, index;
	char character;
	int start_of_word, length_of_word;
	touchPosition touch;

	touchRead(&touch);
	row = (touch.py / 8) + bottomScreen.windowY;
	column = (touch.px / 8) + bottomScreen.windowX;

	// THE INDEX OF THE CHARACTER THAT WAS CLICKED
	pointer = column + (row * bottomScreen.consoleWidth);

	character = bottomScreen.fontBgMap[pointer] - bottomScreen.fontCurPal;
	if (character == ' ' || character == '"' || character == '.' || character == ',' || character == '[' || character == ']') {
		return NULL;
	}

	// MOVE BACKWARDS AND SEARCH FOR A SPACE
	for (index = 0; index <= column; index++) {
		character = bottomScreen.fontBgMap[pointer - index] - bottomScreen.fontCurPal;
		if (character == ' ' || character == '"' || character == '.' || character == ',' || character == '[' || character == ']') {
			// FOUND START OF WORD
			break;
		}
	}
	index--;

	start_of_word = pointer - index;
	length_of_word = index; // THERE IS MORE OF THE WORD TO MEASURE YET

	// SEARCH FORWARD AND LOOK FOR A SPACE
	for (index = 0; index < (bottomScreen.consoleWidth - column); index++) {
		character = bottomScreen.fontBgMap[pointer + index] - bottomScreen.fontCurPal;
		if (character == ' ' || character == '"' || character == '.' || character == ',' || character == '[' || character == ']') {
			// FOUND START OF WORD
			break;
		}
	}
	
	length_of_word += index;

	for (index = 0; index < length_of_word; index++) {
		clicked_word[index] = bottomScreen.fontBgMap[start_of_word + index] - bottomScreen.fontCurPal;
	}
	clicked_word[index] = 0;

	//printf("word is: %s\n", word);	
	return clicked_word;
}
