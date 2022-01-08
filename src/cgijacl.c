/* cgijacl.c --- CGI JACL Interpreter
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
#include "cgijacl.h"
#include "cgi-lib.h"
#include "html-lib.h"
#include "language.h"
#include "types.h"
#include "prototypes.h"
#include "csv.h"

extern struct csv_parser parser_csv;

extern char			text_buffer[];
extern char			*word[];
extern short int	quoted[];
extern short int	punctuated[];
extern int			wp;

extern int			custom_error;
extern int			interrupted;

extern int			it;
extern int			them[];
extern int			her;
extern int			him;
extern int			oops_word;

extern int			proxy_level;

extern int			style_index;

char            include_directory[81] = "\0";
char            temp_directory[81] = "\0";
char            data_directory[81] = "\0";
char            error_log[81] = "\0";
char            access_log[81] = "\0";


char            function_name[81];
char            rpc_function_name[81];
char            override[81];

char            temp_buffer[1024];
char            file_buffer[1024];
char            error_buffer[1024];
char            chunk_buffer[4096];
char            proxy_buffer[1024];

int				start_of_last_command;
int				start_of_this_command;

int			prefer_remote_user = TRUE;
int			cookie_read_successfully;

int             buffer_index = 0;

int             noun[4];
int             player = 0;

int             variable_contents;
int             oec;
int            *object_element_address,
               *object_backup_address;

long            top_of_loop;
long            top_of_while;
long            top_of_do_loop;

FILE           *file = NULL;

char            user_id[81] = "\0";
char            prefix[81] = "\0";
char            cookie_expiry[81] = "21600";
char            game_path[256] = "\0";
char            game_file[256] = "\0";
char            processed_file[256] = "\0";
char            saved_start[256] = "\0";
char            blorb[81] = "\0";

char            game_url[256] = "\0";
llist           entries;
llist	      jacl_cookies;
struct stat     gamestat;

char			oops_buffer[1024];
char			oopsed_current[1024];
char            last_command[1024];
char            current_command[1024];

int             objects, integers, functions, strings;

struct stack_type backup[STACK_SIZE];
struct object_type *object[MAX_OBJECTS];
struct integer_type *integer_table = NULL;
struct cinteger_type *cinteger_table = NULL;
struct constant_type *constant_table = NULL;
struct attribute_type *attribute_table = NULL;
struct string_type *string_table = NULL;
struct string_type *cstring_table = NULL;
struct parameter_type *parameter_table = NULL;
struct function_type *function_table = NULL;
struct function_type *executing_function = NULL;
struct word_type *grammar_table = NULL;
struct synonym_type *synonym_table = NULL;
struct filter_type *filter_table = NULL;

static void version_info(void);

int
main(argc, argv)
	 int             argc;
	 char           *argv[];
{
	int             index;
	char           *last_slash;
	short int       returning_player;

	time_t          tnow;
	time_t          current_last_modified,
	                previous_last_modified;
	struct stat     gamefilestat;

#ifdef WEBJACL
	extern int      wj_port;
	extern char     wj_hostname[];
#endif

	srand((int) time(NULL));

	override[0] = 0;

	sprintf(error_buffer, "CGIJACL Interpreter v%d.%d.%d\n", J_VERSION, J_RELEASE, J_BUILD);
	log_message(error_buffer, PLUS_STDERR);

	if (argc == 1) {
		log_error(NO_GAME, PLUS_STDERR);
		terminate(40);
	}

#ifdef WEBJACL
	if (wj_setup(&argc, argv) == WJ_NOPORT) {
		sprintf(error_buffer, NO_PORT, WJ_DEFAULT_PORT);
		log_message(error_buffer, PLUS_STDERR);
	}

	sprintf(error_buffer, WEBJACL_CONFIGURED, wj_hostname, wj_port);
	log_message(error_buffer, PLUS_STDERR);
#endif

	strcpy(temp_buffer, argv[1]);

#ifdef WIN32
	/* THIS CODE CONVERTS ALL FORWARD SLASHES TO BACK SLASHES AND IS
	 * REQUIRED WHEN COMPILING FOR MS WINDOWS USING VISUAL C++ */

	for (index = 0; index < strlen(temp_buffer); index++) {
		if (temp_buffer[index] == '/')
			temp_buffer[index] = '\\';
	}
#else
	/* THIS CODE CONVERTS ALL BACK SLASHES TO FORWARD SLASHES AND IS
	 * REQUIRED WHEN COMPILING FOR MS WINDOWS USING CYGWIN */

	for (index = 0; index < strlen(temp_buffer); index++) {
		if (temp_buffer[index] == '\\')
			temp_buffer[index] = '/';
	}
#endif

	/* SAVE A COPY OF THE SUPPLIED GAMEFILE NAME WITH ALL SLASHES CHANGED */
	strcpy(game_file, temp_buffer);

	/* FIND THE LAST SLASH IN THE SPECIFIED GAME PATH AND REMOVE THE GAME
	 * FILE SUFFIX IF ANY EXISTS */
	last_slash = (char *) NULL;
	last_slash = strrchr(temp_buffer, '/');
	for (index = strlen(temp_buffer); index >= 0; index--) {
		if (temp_buffer[index] == '/')	/* THERE IS NO SUFFIX */
			break;
		if (temp_buffer[index] == '.') {
			temp_buffer[index] = 0;
			break;
		}
	}

	/* STORE THE GAME PATH AND THE GAME FILENAME PARTS SEPARATELY */
	if (last_slash == (char *) NULL) {
		/* GAME MUST BE IN CURRENT DIRECTORY SO THERE WILL BE NO GAME PATH */
		strcpy(prefix, temp_buffer);
		game_path[0] = 0;
	} else {
		/* STORE THE DIRECTORY THE GAME FILE IS IN WITH THE TRAILING
		 * SLASH IF THERE IS ONE */
		last_slash++;
		strcpy(prefix, last_slash);
		*last_slash = '\0';
		strcpy(game_path, temp_buffer);
	}

	if (game_file[0] == 10) {
		log_error(NO_GAME, PLUS_STDERR);
		terminate(41);
	}

	/* CHECK THE TIMESTAMP ON THE GAME FILE */
	if (stat(game_file, &gamestat) == -1) {
		log_error(NOT_FOUND, PLUS_STDERR);
		terminate(41);
	}

	/* BUILD THE FILE NAME FOR THE JACL ETC LOCATION */
	strcpy(temp_buffer, game_path);
	strcat(temp_buffer, "../etc/cgijacl.conf");

	if ((file = fopen("cgijacl.conf", "r")) != NULL) {
		/* FOUND THE CONFIGURATION FILE IN THE CURRENT DIRECTORY */
		sprintf(error_buffer, "Using configuration file \"./cgijacl.conf\"");
		log_message(error_buffer, PLUS_STDERR);
		read_config_file();
	} else if ((file = fopen(temp_buffer, "r")) != NULL) {
		/* FOUND THE CONFIGURATION FILE IN THE JACL ETC LOCATION */
		sprintf(error_buffer, "Using configuration file \"%s\"", temp_buffer);
		log_message(error_buffer, PLUS_STDERR);
		read_config_file();
	} else if ((file = fopen("/etc/cgijacl.conf", "r")) != NULL) {
		/* FOUND THE CONFIGURATION FILE IN THE GLOBAL ETC DIRECTORY */
		sprintf(error_buffer, "Using configuration file \"/etc/cgijacl.conf\"");
		log_message(error_buffer, PLUS_STDERR);
		read_config_file();
	}

	/* SET DEFAULT FILE LOCATIONS IF NOT SET BY THE USER IN CONFIG */
	if (error_log[0] == 0) {
		strcpy(error_log, game_path);
		strcat(error_log, "../logs/error.log");
		sprintf(error_buffer, "Using default error log \"%s\"", error_log);
		log_message(error_buffer, PLUS_STDERR);
	} else {
		sprintf(error_buffer, "Using configured error log \"%s\"", error_log);
		log_message(error_buffer, PLUS_STDERR);
	}

	if (access_log[0] == 0) {
		strcpy(access_log, game_path);
		strcat(access_log, "../logs/access.log");
		sprintf(error_buffer, "Using default access log \"%s\"", access_log);
		log_message(error_buffer, PLUS_STDERR);
	} else {
		sprintf(error_buffer, "Using configured access log \"%s\"", access_log);
		log_message(error_buffer, PLUS_STDERR);
	}

	if (include_directory[0] == 0) {
		strcpy(include_directory, game_path);
		strcat(include_directory, "include/");
		sprintf(error_buffer, "Using default include location \"%s\"", include_directory);
		log_message(error_buffer, PLUS_STDERR);
	} else {
		sprintf(error_buffer, "Using configured include location \"%s\"", include_directory);
		log_message(error_buffer, PLUS_STDERR);
	}

	if (data_directory[0] == 0) {
		strcpy(data_directory, game_path);
		strcat(data_directory, "data/");
		sprintf(error_buffer, "Using default data location \"%s\"", data_directory);
		log_message(error_buffer, PLUS_STDERR);
	} else {
		sprintf(error_buffer, "Using configured data location \"%s\"", data_directory);
		log_message(error_buffer, PLUS_STDERR);
	}

	if (temp_directory[0] == 0) {
		strcpy(temp_directory, game_path);
		strcat(temp_directory, "temp/");
		sprintf(error_buffer, "Using default temp location \"%s\"", temp_directory);
		log_message(error_buffer, PLUS_STDERR);
	} else {
		sprintf(error_buffer, "Using configured temp location \"%s\"", temp_directory);
		log_message(error_buffer, PLUS_STDERR);
	}

	sprintf(error_buffer, "Cookie expiry set to \"%s\"", cookie_expiry);
	log_message(error_buffer, PLUS_STDERR);

	/* PREPROCESS THE FILE AND WRITE IT OUT TO THE NEW FILE */
	if (jpp() == FALSE) {
		log_error(error_buffer, PLUS_STDERR);
		terminate(42);
	}

	/* ...AND OPEN THE RESULTING FILE AS OUR INPUT */
	if ((file = fopen(processed_file, "r")) == NULL) {
		sprintf(error_buffer, CANT_OPEN, processed_file);
		log_error(error_buffer, PLUS_STDERR);
		terminate(42);
	}

	read_gamefile();

	// INTIALISE THE CSV PARSER
	csv_init(&parser_csv, CSV_APPEND_NULL);
  
	// CODE THAT IS ONLY RUN ONCE WHEN THE GAME IS LOADED
	execute ("+bootstrap");

	/* SAVE THE GAME STATE RIGHT AT THE START TO RESTORE FOR
       NEW USERS INSTEAD OF RESTARTING EVERY TIME */
	sprintf(saved_start, "%s%s-start.saved", temp_directory, prefix);

	if (save_game(saved_start) == FALSE) {
		sprintf(error_buffer, cstring_resolve("CANT_SAVE")->value, saved_start);
		log_error(error_buffer, LOG_ONLY);
	}

	if (object[2] == NULL) {
		log_error(CANT_RUN, PLUS_STDERR);
		terminate(43);
	}

	strcpy (user_id, "<STARTUP>");
	log_error(cstring_resolve("STARTING")->value, LOG_ONLY);

	/* STORE THE GAME FILES LAST MODIFIED TIME */
	stat(argv[1], &gamefilestat);
	previous_last_modified = gamefilestat.st_mtime;

	/* TOP OF RESPONSE LOOP */
	while (FCGI_Accept() >= 0) {
#if WJ_SERVERTYPE==WJ_DEVSERVER
		/* IF THIS IS DEVELOPMENT SERVER, TRY TO RELOAD DATA FILE IF IT
		 * HAS CHANGED SINCE OUT LAST RESTART */

		/* GET THE URL THIS GAME IS BEING CALLED AS 
         * FROM THE ENVIRONMENT VARIABLE SCRIPT_NAME*/
		strcpy (game_url, SCRIPT_NAME);

		/* DETERMINE FILE MODIFICATION TIME */
		stat(argv[1], &gamefilestat);
		current_last_modified = gamefilestat.st_mtime;

		if (current_last_modified != previous_last_modified) {
			log_error(GAME_MODIFIED, LOG_ONLY);

			/* PREPROCESS THE FILE AND WRITE IT OUT TO THE NEW FILE */
			if (jpp() == FALSE) {
				log_error(error_buffer, PLUS_STDERR);
				terminate(42);
			}

			/* CLOSE AND RE_OPEN THE GAME FILE, THEN RELOAD THE DATA */
			if (file != NULL)
				fclose(file);

			if ((file = fopen(processed_file, "r")) == NULL) {
				sprintf(error_buffer, CANT_OPEN, temp_buffer);
				log_error(error_buffer, PLUS_STDERR);
				terminate(42);
			}

			restart_game();

			previous_last_modified = current_last_modified;
		}
#endif

		user_id[0] = (char) 0; 			// CLEAR THE USER_ID
		rpc_function_name[0] = (char) 0;	// CLEAR THE RPC FUNCTION NAME

		read_cgi_input(&entries);
		parse_cookies(&jacl_cookies);
		cookie_read_successfully = FALSE;

		//sprintf (error_buffer, "HTTP_COOKIE: %s", getenv("HTTP_COOKIE"));
		//log_message(error_buffer, PLUS_STDERR);

		//sprintf (error_buffer, "user_id value pulled from cookies: %s", cgi_val(jacl_cookies, "user_id"));
		//log_message(error_buffer, PLUS_STDERR);

		if (cgi_val(entries, "user_id") != NULL || cgi_val(jacl_cookies, "user_id") != NULL) {
			// A user_id HAS BEEN PASSED TO THIS REQUEST VIA A PARMETER OR A COOKIE
			if (prefer_remote_user == TRUE && REMOTE_USER != NULL && strcmp("", REMOTE_USER)) {
			    /* PREFER REMOTE_USER FOR POTENTIAL SECURE SITES. */
				strcpy (user_id, REMOTE_USER);
				//sprintf(error_buffer, "Using user_id %s from REMOTE_USER.", user_id);
				//log_message(error_buffer, PLUS_STDERR);
				REMOTE_USER_USED->value = TRUE;
				returning_player = TRUE;
			} else {
				// THERE IS NO REMOTE_USER OR prefer_remote_user IS SET TO FALSE
				// SO USE THE PASSED user_id
				if (cgi_val(jacl_cookies, "user_id") != NULL) {
					strcpy(user_id, cgi_val(jacl_cookies, "user_id"));
					REMOTE_USER_USED->value = TRUE;
					cookie_read_successfully = TRUE;
					//sprintf(error_buffer, "Using user_id %s from cookie.", user_id);
					//log_message(error_buffer, PLUS_STDERR);
				} else {
					strcpy(user_id, cgi_val(entries, "user_id"));
					REMOTE_USER_USED->value = FALSE;
					//sprintf(error_buffer, "Using user_id %s from parameter.", user_id);
					//log_message(error_buffer, PLUS_STDERR);
				}
				returning_player = TRUE;
			}
		} else if (REMOTE_USER != NULL && strcmp("", REMOTE_USER)) {
			// REMOTE_USER IS SET AND user_id IS NULL SO USE REMOTE_USER
			strcpy (user_id, REMOTE_USER);
			//sprintf(error_buffer, "Using user_id %s from REMOTE_USER.", user_id);
			//log_message(error_buffer, PLUS_STDERR);
			REMOTE_USER_USED->value = TRUE;
			returning_player = TRUE;
		} else {
			// REMOTE_USER ISN'T SET AND user_id IS BLANK SO CREATE A NEW user_id
			sprintf(user_id, "%d-%d",
					(1 + (int) ((float) 58408 * rand() / (RAND_MAX + 1.0))),
					(1 + (int) ((float) 256 * rand() / (RAND_MAX + 1.0))));

			// THIS IS THE FIRST COMMAND OF A NEW GAME
			returning_player = FALSE;

			//sprintf(error_buffer, "Created user_id %s for new user.", user_id);
			//log_message(error_buffer, PLUS_STDERR);

			// SET THIS TO TRUE ASSUMING THAT COOKIES ARE ENABLED
			REMOTE_USER_USED->value = TRUE; 
		}

		if (returning_player == TRUE) {
			sprintf(temp_buffer, "%s%s-%s.auto", temp_directory, prefix, user_id);

			/* AS HTTP IS STATELESS, RELOAD THE PLAYER'S GAME IN PROGRESS
			 * BEFORE PROCESSING EACH COMMAND */
			if (restore_game(temp_buffer, FALSE)) {
				returning_player = TRUE;
			} else {
				sprintf(error_buffer, "Unable to restore saved file for program \"%s\" and returning user \"%s\"", prefix, user_id);
				log_error(error_buffer, LOG_ONLY);
				returning_player = FALSE;
			}
		}

		if (returning_player == FALSE) {
			// THIS TRANSACTION IS EITHER NOT ASSOCIATED WITH A REMOTE_USER OR user_id
			// OR THE LOADING OF THE ASSOCIATED SAVED GAME FAILED
			/* TRY TO RESTART THE GAME BY RESTORING THE saved_start */
			if (restore_game(saved_start, FALSE) == FALSE) {
				/* THIS HAS FAILED, USED THE LESS EFFICIENT restart_game
				 * FUNCTION INSTEAD */
				restart_game();
			}
		}

		/* COPY THE VALUE OF ANY DEFINED PARAMETERS FROM THE HTTP 
		 * PARAMETERS INTO THE SPECIFIED JACL INTEGER ELEMENTS */
		update_parameters();

		// OUTPUT THE HTTP HEADER
		puts("Content-type: text/html");

		if (cookie_read_successfully == FALSE) {
			// COOKIE WASN'T READ SO THIS IS EITHER THE FIRST TRANSACTION
			// OR COOKIES AREN'T SUPPORTED BY THE USER'S CLIENT
			printf("Set-Cookie: user_id=%s; SameSite=Strict; Max-Age=%s\n", user_id, cookie_expiry);
		}
		puts("Expires: -1\n");

		/* RESET GLOBAL VARIABLES THAT ARE INTERNAL TO THE INTERPRETER */
		style_index = 0;

		/* DISPLAY THE HEADER OF THE HTML PAGE */
		if (cgi_val(entries, "rpc") == NULL && 
			(cgi_val(entries, "ajax") == NULL || strcmp(cgi_val(entries, "ajax"), "true"))) {
			if (execute("+header") == FALSE) {
				default_header();
			}
		}

		if (returning_player == FALSE) {
			/* THIS IS THE START OF A NEW GAME, NOT AN EXISTING USER 
			 * CONTINUING A GAME */

			/* DISPLAY THE GAMES INTRODUCTION AS THIS IS THE FIRST MOVE */
			execute("+intro");

    		/* DUMMY RETRIEVE OF 'HERE' FOR TESTING OF GAME STATE */
    		get_here();

			/* TIME PASSES BEFORE THE PLAYER SEES THE FIRST COMMAND PROMPT */
			eachturn();

		}

		if (returning_player 
			|| (cgi_val(entries, "command") != NULL)
			|| (cgi_val(entries, "rpc") != NULL)) {
			/* THIS MOVE IS A CONTINUATION OF A GAME THAT IS IN PROGRESS */
			TIME->value = TRUE;
			custom_error = FALSE;

			/* PUT DIRECT FUNCTION CALL CHECKS HERE */
			if (cgi_val(entries, "rpc") != NULL) {
				// ALL FUNCTION CALLS ARE AJAX RPC BY DEFINITION

				if (!strcmp(cgi_val(entries, "rpc"), "timer")) {
					execute("+timer");
				} else if (!strcmp(cgi_val(entries, "rpc"), "ajax")) {
					execute("+ajax");
				} else if (!strcmp(cgi_val(entries, "rpc"), "eachturn")) {
					/* CALL THE GLOBAL EACHTURN FUNCTION THEN ASSOCIATED */
					execute("+eachturn");
					strcpy(function_name, "eachturn_");
					strcat(function_name, object[HERE]->label);
					execute(function_name);
				} else {
					// CALL +rpc, BUT SET THE NAME THE FUNCTION IS CALLED AS
					// TO THE ONE SUPPLIED. THIS IS FOR SECURITY PURPOSES.
					// IT PREVENTS ANY FUNCTION BEING CALLED AT WILL, BUT
					// PROVIDES A CLEAN INTERFACE FOR THE PURPOSE OF THE CALL
					// TO BE HANDLED INSIDE +rpc
					strcpy(rpc_function_name, "+");
					strncat(rpc_function_name, cgi_val(entries, "rpc"), 80);
					execute("+rpc");
				}
			} else {
				/* GET THE REQUEST PARAMETERS CONTAINING THE PLAYER'S MOVE */
				if (cgi_val(entries, "verb") != NULL) {
					strcpy(text_buffer, cgi_val(entries, "verb"));
	
					if (cgi_val(entries, "noun") != NULL)
						strcat(text_buffer, cgi_val(entries, "noun"));
				} else {
					if (cgi_val(entries, "command") != NULL)
						strcpy(text_buffer, cgi_val(entries, "command"));
					else
						strcpy(text_buffer, "");
				}
	
				/* CONVERT THE COMMAND TO LOWER CASE AND REPLACE ANY ANGLE 
				 * BRACKETS WITH SPACES */
				for (index = 0; index < 1023; index++) {
					if (text_buffer[index] == 0) {
						break;
					} else if (text_buffer[index] == '<' || text_buffer[index] == '>') {
						text_buffer[index] = ' ';
					}
				}
	
				time(&tnow);
	
				/* LOG THIS MOVE TO THE ACCESS LOG */
				sprintf(temp_buffer, "%s - %s - %s - %s\n", strip_return(ctime(&tnow)), user_id, prefix, text_buffer);
				log_access(temp_buffer);
	
				strcpy(current_command, text_buffer);
	
				command_encapsulate();
				jacl_truncate();
	
				/* IF THERE IS NO COMMAND, SET THE COMMAND TO 'blankjacl' SO 
				 * THE GAME CAN CODE A CUSTOM RESPONSE */
				if (word[0] == NULL) {
					strcpy(text_buffer, "blankjacl");
					encapsulate();
				}
	
		     /* SET THE INTEGER INTERRUPTED TO FALSE. IF THIS IS SET TO
			 * TRUE BY ANY COMMAND, FURTHER PROCESSING WILL STOP */
	        	INTERRUPTED->value = FALSE;
	
	        	interrupted = FALSE;
	
				/* CALL THE PARSER TO START PROCESSING THE COMMAND */
				preparse();
	
		        if (current_command[0] != 0) {
					strcpy(last_command, current_command);
				}
			}
		}

		/* DISPLAY THE FOOTER OF THE HTML PAGE */
		if (cgi_val(entries, "rpc") == NULL &&
			(cgi_val(entries, "ajax") == NULL || strcmp(cgi_val(entries, "ajax"), "true"))) {
			if (execute("+footer") == FALSE) {
				default_footer();
			}
		}

		/* SAVE THE GAME STATE AFTER THIS MOVE TO BE
		   RESTORED BEFORE THE PLAYER'S NEXT MOVE */
		sprintf(temp_buffer, "%s%s-%s.auto", temp_directory, prefix, user_id);
		if (save_game(temp_buffer) == FALSE) {
			sprintf(error_buffer, cstring_resolve("CANT_SAVE")->value, prefix, temp_buffer);
			log_error(error_buffer, PLUS_STDOUT);
		}

		list_clear(&entries);

	}

    return (1);
}

void
default_header()
{
	/* THIS HEADER IS DISPLAYED IS NO CUSTOM ONE IS PROVIDED */

	puts("<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\"\n\"http://www.w3.org/TR/html4/loose.dtd\">\n");
	puts("<html><head>");
	puts("<title>");
	printf("%s", cstring_resolve("game_title")->value);
	puts("</title>");
	puts("<script language=\"JavaScript\">");
	puts("<!--");
	puts("function userCommand() {");
	puts("var xhReq = createXMLHttpRequest();");
	puts("if(xhReq == null) { return true; }");
	puts("var user_id = document.JACLGameForm.user_id.value;");
	puts("var command = document.JACLGameForm.command.value;");
	printf("xhReq.open(\"GET\", \"%s", game_url);
	puts("?user_id=\"+user_id+\"&command=\"+command+\"&ajax=true\", false);");
	puts("xhReq.send(null);");
	puts("var serverResponse = xhReq.responseText;");
	puts("var maintext = document.getElementById(\"maintext\");");
	puts("maintext.innerHTML += \"<br><b>&gt;\" + command + \"</b><br>\" + serverResponse;");
	puts("var main = document.getElementById(\"main\");");
	puts("main.scrollTop = main.scrollHeight;");
	puts("document.JACLGameForm.command.value = \"\";");
	puts("putFocus(0,0);");
	puts("return false; }");
	puts("function putFocus(formInst, elementInst) {");
	puts("if (document.forms.length > 0) {");
	puts("document.forms[formInst].elements[elementInst].focus(); }}");
	puts("function createXMLHttpRequest() {");
	puts("try { return new XMLHttpRequest(); } catch(e) {}");
	puts("try { return new ActiveXObject(\"Msxml2.XMLHTTP\"); } catch (e) {}");
	puts("return null; }");
	puts("-->");
	puts("</script>");
	puts("<style> <!--");
	puts("#footer { position:absolute; bottom:0; left:0; right:0;  height: 70px; background-color: #bbbbbb; text-align: center; }");
	puts("#main { position:absolute; left:0px; top:0px; right:0px; overflow:auto; bottom: 70px;}");
	puts("div.maintext { font-family: Verdana, Arial, Sanserif; padding-top: 20px; padding-bottom: 20px; padding-left: 50px; padding-right: 50px; font-size: 12pt; overflow: auto; }");
	puts("#JACLCommandPrompt { width: 95%; margin: 25px 10px 10px 10px;}");
	puts("--> </style>");
	puts("</head><body onLoad=\"putFocus(0, 0);\">");
	puts("<div id=\"main\">");
	puts("<div id=\"maintext\" class=\"maintext\">");
}

void
default_footer()
{
	/* THIS FOOTER IS DISPLAYED IS NO CUSTOM ONE IS PROVIDED */

	puts("</div>\n");
	puts("</div>\n");
	puts("<div id=\"footer\" class=\"footer\">");
	puts("<form name=\"JACLGameForm\" method=get onsubmit=\"return userCommand();\">");
	puts("<input id=\"JACLCommandPrompt\" type=text name=\"command\">");
	printf("<input type=hidden name=\"user_id\" value=\"%s\">\n", user_id);
	puts("</form></div></body></html>");
}

void
update_parameters()
{
	/* THIS FUNCTION CHECKS FOR ANY REQUEST PARAMETERS THAT ARE PASSED 
	 * BEYOND THE STANDARD verb, noun and command ONES. OTHER PARAMETERS
	 * THAT ARE DEFINED HAVE THEIR INTEGER-ONLY VALUES COPIED INTO THE
	 * SPECIFIED ELEMENT */
	struct parameter_type *pointer = parameter_table;
	int	*container;
	struct string_type *string = NULL;

	if (pointer == NULL)
		return;

	// LOOP THROUGH ALL THE DEFINED PARAMETERS
	do { 
		// DOES	THE CURRENT PARAMETER HAVE A VALUE PASSED WITH THIS REQUEST?
		if (cgi_val(entries, pointer->name) != NULL) {
			// YES, DETERMINE THE TYPE OF CONTAINER THE PARAMETER MATCHES

			// IS IT AN INTEGER?
			container = container_resolve(pointer->container);
			if (container != NULL) {
				// YES, THE PARAMETER POINTS TO AN INTEGER CONTAINER
				if (validate(cgi_val(entries, pointer->name))) {
					*container = atoi(cgi_val(entries, pointer->name));
					if (*container < pointer->low)
						*container = pointer->low;
					if (*container > pointer->high)
						*container = pointer->high;
				} else {
					sprintf(error_buffer, BAD_VALUE, cgi_val(entries, pointer->name), pointer->container);
					log_error(error_buffer, PLUS_STDOUT);
				}
				
			} else {
				// IS IT A STIRNG?
				string = string_resolve(pointer->container);
				if (string != NULL) {
					strncpy (string->value, cgi_val(entries, pointer->name), 1023);
				} else {
					// APPARENTLY IT'S NEITHER AN INTEGER OR A STRING, DISPLAY AN ERROR
					sprintf(error_buffer, BAD_PARAMETER, pointer->container, pointer->name);
					log_error(error_buffer, PLUS_STDOUT);
				}
			}
			
		}
		pointer = pointer->next_parameter;
	} while (pointer != NULL);
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

        clear_cstring("command");

        position = wp;

        while (word[position] != NULL && strcmp(word[position], cstring_resolve("THEN_WORD")->value)) {
            add_cstring ("command", word[position]);
            position++;
        };

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

	/* START CHECKING THE PLAYER'S COMMAND FOR SYSTEM COMMANDS */
	if (!strcmp(word[wp], cstring_resolve("RESTART_WORD")->value)) {
		if (execute("+restart_game") == FALSE) {
		    TIME->value = TRUE;
		    if (restore_game(saved_start, FALSE) == FALSE) {
		    	restart_game();
		    }

		    execute("+intro");
		    eachturn();
		}
	} else if (!strcmp(word[wp], cstring_resolve("OOPS_WORD")->value)) {
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
				strcpy(oops_buffer, word[wp]);
				strcpy(text_buffer, last_command);

				command_encapsulate();

				jacl_truncate();
				word[oops_word] = (char *) &oops_buffer;

				/* BUILD A PLAIN STRING REPRESENTING THE NEW COMMAND */
				last_command[0] = 0;
				index = 0;

				while (word[index] != NULL) {
					if (last_command[0] != 0) {
						strcat(last_command, " ");
					}

					strcat(last_command, word[index]);

					index++;
				}

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
			strcpy(current_command, last_command);
			strcpy(text_buffer, last_command);
			command_encapsulate();
			jacl_truncate();
			wp = start_of_last_command;
			word_check();
		}
	} else if (!strcmp(word[wp], cstring_resolve("UNDO_WORD")->value)) {
		write_text("Undo not currently supported under web interface.^");
		TIME->value = FALSE;
#ifdef WEBJACL
	} else if (!strcmp(word[wp], cstring_resolve("QUIT_WORD")->value)
				|| !strcmp(word[wp], "quit")) {
        terminate(0);
#endif
	} else if (!strcmp(word[wp], cstring_resolve("INFO_WORD")->value)
			   || !strcmp(word[wp], "version")) {
		version_info();
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
read_config_file()
{
	fgets(text_buffer, 80, file);

	while (!feof(file)) {
		encapsulate();

		if (word[0] == NULL) {
			// DO NOTHING
		} else if (!strcmp(word[0], "temp")) {
			if (word[1] != NULL) {
				strncpy(temp_directory, word[1], 80);
				if (temp_directory[strlen(temp_directory) - 1] != '/')
					strcat(temp_directory, "/");
			}
		} else if (!strcmp(word[0], "prefer_remote_user")) {
			prefer_remote_user = TRUE;
		} else if (!strcmp(word[0], "ignore_remote_user")) {
			prefer_remote_user = FALSE;
		} else if (!strcmp(word[0], "data")) {
			if (word[1] != NULL) {
				strncpy(data_directory, word[1], 80);
				if (data_directory[strlen(data_directory) - 1] != '/')
					strcat(data_directory, "/");
			}
		} else if (!strcmp(word[0], "include")) {
			if (word[1] != NULL) {
				strncpy(include_directory, word[1], 80);
				if (include_directory[strlen(include_directory) - 1] != '/')
					strcat(include_directory, "/");
			}
		} else if (!strcmp(word[0], "error_log")) {
			if (word[1] != NULL) {
				strncpy(error_log, word[1], 80);
				if (error_log[strlen(error_log) - 1] == '/')
					strcat(error_log, "error.log");
			}
		} else if (!strcmp(word[0], "access_log")) {
			if (word[1] != NULL) {
				strncpy(access_log, word[1], 80);
				if (access_log[strlen(access_log) - 1] == '/')
					strcat(access_log, "access.log");
			}
		} else if (!strcmp(word[0], "cookie_expiry")) {
			if (word[1] != NULL) {
				strncpy(cookie_expiry, word[1], 80);
			}
		}
		fgets(text_buffer, 80, file);
	}

	fclose(file);
	file = NULL;
}

void
version_info()
{
	char            buffer[80];

	sprintf(buffer, "<p>CGIJACL Interpreter v%d.%d.%d ", J_VERSION, J_RELEASE,
			J_BUILD);
	write_text(buffer);
	sprintf(buffer, "/ %d object. ", MAX_OBJECTS);
	write_text(buffer);
	write_text("Copyright &copy; 1992-2008 Stuart Allen.</p>");
	sprintf(buffer, "<p>OBJECTS DEFINED:   %d</p>", objects);
	write_text(buffer);
}

void
write_text(tout_buffer)
	 char            tout_buffer[];
{
	int             index;

	if (!strcmp(tout_buffer, "tilde")) {
		chunk_buffer[buffer_index] = '~';
		buffer_index++;
		chunk_buffer[buffer_index] = 0;
		return;
	} else if (!strcmp(tout_buffer, "caret")) {
		chunk_buffer[buffer_index] = '^';
		buffer_index++;
		chunk_buffer[buffer_index] = 0;
		return;
	} else if (!strcmp(tout_buffer, "circumflex")) {
		chunk_buffer[buffer_index] = '^';
		buffer_index++;
		chunk_buffer[buffer_index] = 0;
		return;
	} else if (!strcmp(tout_buffer, "lessthan")) {
		strcat(chunk_buffer, "&lt;");
		buffer_index = buffer_index + 4;
		chunk_buffer[buffer_index] = 0;
		return;
	} else if (!strcmp(tout_buffer, "greaterthan")) {
		strcat(chunk_buffer, "&gt;");
		buffer_index = buffer_index + 4;
		chunk_buffer[buffer_index] = 0;
		return;
	}

	for (index = 0; index < strlen(tout_buffer); index++) {
		if (tout_buffer[index] == '^') {
			chunk_buffer[buffer_index] = 0;
			if (integer_resolve("linebreaks")->value) {
				printf("%s<br>\n", chunk_buffer);
			} else {
				printf("%s\n", chunk_buffer);
			}
			buffer_index = 0;
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
	printf("%s", chunk_buffer);
	chunk_buffer[0] = 0;
	buffer_index = 0;
}

int
restore_interaction(filename)
    char        *filename;
{

    if (filename == NULL) {
		sprintf(file_buffer, "%s%s-%s-bookmark", temp_directory, prefix, user_id);
    } else {
		sprintf(file_buffer, "%s%s-%s-%s", temp_directory, prefix, user_id, text_of(filename));
	}

    if (restore_game(file_buffer, TRUE) == FALSE) {
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
		sprintf(file_buffer, "%s%s-%s-bookmark", temp_directory, prefix, user_id);
    } else {
		sprintf(file_buffer, "%s%s-%s-%s", temp_directory, prefix, user_id, text_of(filename));
	}

    if (save_game(file_buffer)) {
        return (TRUE);
    } else {
        write_text(cstring_resolve("CANT_SAVE")->value);
        return (FALSE);
    }
}

void
jacl_sleep(unsigned int mseconds)
{
    int multiplier = CLOCKS_PER_SEC / 1000;

    /* WAIT FOR A GIVEN NUMBER OF MILLISECONDS */
    clock_t goal = (mseconds * multiplier) + clock();
    while (goal > clock());
}

