#include <stdio.h>
#include <stdlib.h>
#include "glk.h"
#include "cheapglk.h"
#include "glkstart.h"

int gli_screenwidth = 80;
int gli_screenheight = 24; 
int gli_utf8output = FALSE;
int gli_utf8input = FALSE;

static int inittime = FALSE;

int main(int argc, char *argv[])
{
    int ix, jx, val;
    int errflag = 0;
    glkunix_startup_t startdata;
    
    /* Test for compile-time errors. If one of these spouts off, you
        must edit glk.h and recompile. */
    if (sizeof(glui32) != 4) {
        printf("Compile-time error: glui32 is not a 32-bit value. Please fix glk.h.\n");
        return 1;
    }
    if ((glui32)(-1) < 0) {
        printf("Compile-time error: glui32 is not unsigned. Please fix glk.h.\n");
        return 1;
    }
    
    /* Now some argument-parsing. This is probably going to hurt. */
    startdata.argc = 0;
    startdata.argv = (char **)malloc(argc * sizeof(char *));
    
    /* Copy in the program name. */
    startdata.argv[startdata.argc] = argv[0];
    startdata.argc++;
    
    for (ix=1; ix<argc && !errflag; ix++) {
        glkunix_argumentlist_t *argform;
        int inarglist = FALSE;
        char *cx;
        
        for (argform = glkunix_arguments; 
            argform->argtype != glkunix_arg_End && !errflag; 
            argform++) {
            
            if (argform->name[0] == '\0') {
                if (argv[ix][0] != '-') {
                    startdata.argv[startdata.argc] = argv[ix];
                    startdata.argc++;
                    inarglist = TRUE;
                }
            }
            else if ((argform->argtype == glkunix_arg_NumberValue)
                && !strncmp(argv[ix], argform->name, strlen(argform->name))
                && (cx = argv[ix] + strlen(argform->name))
                && (atoi(cx) != 0 || cx[0] == '0')) {
                startdata.argv[startdata.argc] = argv[ix];
                startdata.argc++;
                inarglist = TRUE;
            }
            else if (!strcmp(argv[ix], argform->name)) {
                int numeat = 0;
                
                if (argform->argtype == glkunix_arg_ValueFollows) {
                    if (ix+1 >= argc) {
                        printf("%s: %s must be followed by a value\n", 
                            argv[0], argform->name);
                        errflag = TRUE;
                        break;
                    }
                    numeat = 2;
                }
                else if (argform->argtype == glkunix_arg_NoValue) {
                    numeat = 1;
                }
                else if (argform->argtype == glkunix_arg_ValueCanFollow) {
                    if (ix+1 < argc && argv[ix+1][0] != '-') {
                        numeat = 2;
                    }
                    else {
                        numeat = 1;
                    }
                }
                else if (argform->argtype == glkunix_arg_NumberValue) {
                    if (ix+1 >= argc
                        || (atoi(argv[ix+1]) == 0 && argv[ix+1][0] != '0')) {
                        printf("%s: %s must be followed by a number\n", 
                            argv[0], argform->name);
                        errflag = TRUE;
                        break;
                    }
                    numeat = 2;
                }
                else {
                    errflag = TRUE;
                    break;
                }
                
                for (jx=0; jx<numeat; jx++) {
                    startdata.argv[startdata.argc] = argv[ix];
                    startdata.argc++;
                    if (jx+1 < numeat)
                        ix++;
                }
                inarglist = TRUE;
                break;
            }
        }
        if (inarglist || errflag)
            continue;
            
        if (argv[ix][0] == '-') {
            switch (argv[ix][1]) {
                case 'w':
                    val = 0;
                    if (argv[ix][2]) 
                        val = atoi(argv[ix]+2);
                    else {
                        ix++;
                        if (ix<argc) 
                            val = atoi(argv[ix+1]);
                    }
                    if (val < 8)
                        errflag = 1;
                    else
                        gli_screenwidth = val;
                    break;
                case 'h':
                    val = 0;
                    if (argv[ix][2]) 
                        val = atoi(argv[ix]+2);
                    else {
                        ix++;
                        if (ix<argc) 
                            val = atoi(argv[ix+1]);
                    }
                    if (val < 2)
                        errflag = 1;
                    else
                        gli_screenheight = val;
                    break;
                case 'u':
                    if (argv[ix][2]) {
                        if (argv[ix][2] == 'i') 
                            gli_utf8input = TRUE;
                        else if (argv[ix][2] == 'o')
                            gli_utf8output = TRUE;
                        else
                            errflag = 1;
                    }
                    else {
                        gli_utf8output = TRUE;
                        gli_utf8input = TRUE;
                    }
                    break;
                default:
                    printf("%s: unknown option: %s\n", argv[0], argv[ix]);
                    errflag = TRUE;
                    break;
            }
        }
    }

    if (errflag) {
        printf("usage: %s -w WIDTH -h HEIGHT -u[i|o]\n", argv[0]);
        if (glkunix_arguments[0].argtype != glkunix_arg_End) {
            glkunix_argumentlist_t *argform;
            printf("game options:\n");
            for (argform = glkunix_arguments; 
                argform->argtype != glkunix_arg_End; 
                argform++) {
                printf("  %s\n", argform->desc);
            }
        }
        return 1;
    }
    
    /* Initialize things. */
    gli_initialize_misc();
    
    inittime = TRUE;
    if (!glkunix_startup_code(&startdata)) {
        glk_exit();
    }
    inittime = FALSE;

    printf("Welcome to the Cheap Glk Implementation, library version %s.\n\n", 
        LIBRARY_VERSION);
    glk_main();
    glk_exit();
    
    /* glk_exit() doesn't return, but the compiler may kvetch if main()
        doesn't seem to return a value. */
    return 0;
}

strid_t glkunix_stream_open_pathname(char *pathname, glui32 textmode, 
    glui32 rock)
{
    if (!inittime)
        return 0;
    return gli_stream_open_pathname(pathname, (textmode != 0), rock);
}
