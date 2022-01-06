/*
   winglk_startup.c: Windows-specific code for the Glk examples.
*/

#include "jacl.h"
#include "prototypes.h"
#include "glk.h"
#include "WinGlk.h"
#include "language.h"

int					jpp_error = FALSE;

extern strid_t 		game_stream;
extern char			game_file[];
extern char			game_path[];
extern char			prefix[];
extern char			temp_buffer[];
extern char			processed_file[];
extern char			error_buffer[];

extern short int    encrypt;
extern short int    release;

/* PROTOTYPE FOR NEEDED UTILITY FUNCTION */
void create_paths(); 

int 
winglk_startup_code(const char* cmdline)
{
	char 		*filename;
    char		*argument;
	frefid_t 	game_fref;

	/* THIS APPLICATION NAME IS USED WHEN STORING USER PREFERENCES IN
	 * THE WINDOWS REGISTRY */
	sprintf(temp_buffer, "JACL v%d.%d.%d", J_VERSION, J_RELEASE, J_BUILD);
   	winglk_app_set_name(temp_buffer);

	/* SET THE TEXT FOR THE FIRST SENTENCE OF THE ABOUT BOX */
	sprintf(temp_buffer, "JACL v%d.%d.%d by Stuart Allen.", J_VERSION, J_RELEASE, J_BUILD);
	winglk_set_about_text(temp_buffer);

	/* CHECK FOR ANY COMMAND LINE ARGUMENTS AND REMOVE THEM */
	if (cmdline != NULL) {
    	if ((argument = strstr(cmdline, "-noencrypt")) != NULL) {
			encrypt = FALSE;
			*argument = 0;
        } else if ((argument = strstr(cmdline, "-release")) != NULL) {
			release = TRUE;
			*argument = 0;
		}

	}

	/* PROCESS THE COMMAND LINE AND GET A FILE NAME FROM THAT.
     * IF THAT FAILS, PROMPT THE USER WITH A FILE DIALOG. */  
	filename = (char*)winglk_get_initial_filename(cmdline, "JACL", "JACL Files (.jacl;.j2)|*.jacl;*.j2|All Files (*.*)|*.*||");

	if (filename == NULL) {
		sprintf (error_buffer, "%s^", NO_GAME);
		jpp_error = TRUE;

		/* WE NEED TO RETURN TRUE HERE SO THE INTERPRETER WILL OPEN A
		 * GLK WINDOWS TO DISPLAY THE ERROR MESSAGE IN */
		return (1);
	}

	/* SETUP ALL THE EXPECTED PATHS */
	create_paths(filename);

	/* SET THE BASE NAME OF FILE NAMES TO BE USED FOR WALKTHRUS ETC */
	sglk_set_basename(prefix);

	/* SET THE LOCATION OF THE BLORB FILE TO THE FULL PATH OF THE GAME FILE */
	winglk_set_resource_directory(game_path);

	/* PREPROCESS THE FILE AND WRITE IT OUT TO THE NEW FILE */
	/* WARNING: THIS FUNCTION USES stdio FUNCTIONS TO CREATE FILES
     * IN SUBDIRECTORIES. IT IS PORTABLE ACROSS MODERN DESKTOPS, IE
	 * WINDOWS, MAC, UNIX ETC, BUT IT'S NOT GLK CODE... */
	if (jpp() == FALSE) {
		jpp_error = TRUE;

		/* WE NEED TO RETURN TRUE HERE SO THE INTERPRETER WILL OPEN A
		 * GLK WINDOWS TO DISPLAY THE ERROR MESSAGE IN */
		return (1);
	}

	/* CREATE A FILE REFERENCE TO THE PROCESSED FILE */
	game_fref = winglk_fileref_create_by_name(fileusage_BinaryMode, processed_file, 0, 0);

	if (game_fref == NULL) {
		sprintf (error_buffer, "%s^", NOT_FOUND);
		jpp_error = TRUE;

		/* WE NEED TO RETURN TRUE HERE SO THE INTERPRETER WILL OPEN A
		 * GLK WINDOWS TO DISPLAY THE ERROR MESSAGE IN */
		return (1);
	}

	/* CREATE A STREAM FROM THE FILE REFERENCE */
	game_stream = glk_stream_open_file(game_fref, filemode_Read, 0);

	if (game_stream == NULL) {
		strcpy (error_buffer, NOT_FOUND);
		jpp_error = TRUE;

		/* WE NEED TO RETURN TRUE HERE SO THE INTERPRETER WILL OPEN A
		 * GLK WINDOWS TO DISPLAY THE ERROR MESSAGE IN */
		return (1);
	}
 
	/* LOAD A GAME-SPECIFIC GAME CONFIG FILE, IF ONE EXISTS */
	winglk_load_config_file(prefix);

	return (1);
}
