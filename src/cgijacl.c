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
#include "interpreter.h"
#include "parser.h"
#include "encapsulate.h"
#include "auth.h"
#include "errno.h"
#include <dirent.h>

extern int            style_index;

/* Walk include_directory and return the newest mtime among its files.
 * Used by the FCGI request loop to invalidate its preprocessed cache
 * when a shared .library is modified even though the game's own .jacl
 * is unchanged — without this, `git pull`s that only touch libraries
 * don't take effect until fcgijacl is restarted. */
static time_t
newest_include_mtime(void)
{
    DIR *dir = opendir(include_directory);
    if (dir == NULL) return 0;
    time_t newest = 0;
    struct dirent *ent;
    char fullpath[1024];
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        snprintf(fullpath, sizeof(fullpath), "%s%s",
                 include_directory, ent->d_name);
        struct stat st;
        if (stat(fullpath, &st) == 0 && st.st_mtime > newest) {
            newest = st.st_mtime;
        }
    }
    closedir(dir);
    return newest;
}

/* Returns 1 if s is a safe identifier for use in a file path or
 * HTTP header value: non-empty, ASCII [A-Za-z0-9_.-], no longer than
 * max-1 bytes (room for NUL). Used to gate every user_id assignment so
 * a malicious cookie/parameter can neither traverse out of
 * temp_directory nor inject CRLF into Set-Cookie. */
static int
is_safe_user_id(const char *s, size_t max)
{
    size_t i;
    if (s == NULL || s[0] == 0) return 0;
    for (i = 0; s[i] != 0; i++) {
        unsigned char c = (unsigned char) s[i];
        if (i + 1 >= max) return 0;
        if (!((c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || c == '.'))
            return 0;
    }
    return 1;
}

/* Same charset as is_safe_user_id, and additionally rejects ".." to
 * defang path-traversal attempts through save-game filenames coming
 * from game-supplied text (text_of() in save_interaction). */
static int
is_safe_filename_component(const char *s, size_t max)
{
    if (!is_safe_user_id(s, max)) return 0;
    if (strstr(s, "..") != NULL) return 0;
    return 1;
}

/* Fill buf with "anon_" + 32 hex chars from /dev/urandom + NUL.
 * Returns 0 on success, -1 on failure (caller should treat as fatal
 * for the request rather than fall back to a guessable ID). The
 * previous implementation used rand() seeded with time(NULL), giving
 * ~32k entropy guessable from the request timestamp. */
static int
secure_random_user_id(char *buf, size_t buflen)
{
    /* "anon_" (5) + 32 hex + NUL = 38 */
    if (buflen < 38) return -1;
    FILE *fp = fopen("/dev/urandom", "rb");
    if (fp == NULL) return -1;
    unsigned char raw[16];
    size_t got = fread(raw, 1, sizeof(raw), fp);
    fclose(fp);
    if (got != sizeof(raw)) return -1;
    static const char hex[] = "0123456789abcdef";
    memcpy(buf, "anon_", 5);
    size_t i;
    for (i = 0; i < sizeof(raw); i++) {
        buf[5 + i * 2]     = hex[(raw[i] >> 4) & 0xF];
        buf[5 + i * 2 + 1] = hex[raw[i] & 0xF];
    }
    buf[5 + sizeof(raw) * 2] = 0;
    return 0;
}

/* Returns 1 for yes, 0 for no, -1 for invalid */
static int
resolve_yes_or_no(const char *input)
{
    char lower[256];
    int i;

    strncpy(lower, input, 255);
    lower[255] = 0;
    for (i = 0; lower[i]; i++)
        lower[i] = tolower((int)lower[i]);

    /* Strip leading whitespace */
    char *s = lower;
    while (*s == ' ' || *s == '\t') s++;

    /* Strip trailing whitespace/newlines */
    i = strlen(s) - 1;
    while (i >= 0 && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
        s[i--] = 0;

    if (!strcmp(s, "yes") || !strcmp(s, "y") || !strcmp(s, "ya") ||
        !strcmp(s, "oui") || !strcmp(s, "ja") || !strcmp(s, "si"))
        return 1;

    if (!strcmp(s, "no") || !strcmp(s, "n") || !strcmp(s, "tidak") ||
        !strcmp(s, "non") || !strcmp(s, "nein"))
        return 0;

    return -1;
}

/* Defined in interpreter.c next to the updatestatus opcode handler. */
void            web_render_status_bar(void);
/* Counter incremented inside web_render_status_bar() so the end-of-
 * ajax safety net below can tell whether a turn already emitted a
 * status bar; reset to 0 at the top of every request. Definition
 * lives further down with the other web_status_* state. */
extern int      web_status_emit_count;

char            include_directory[81] = "\0";
char            temp_directory[81] = "\0";
char            data_directory[81] = "\0";
char            error_log[81] = "\0";
char            access_log[81] = "\0";


char            function_name[81];
char            rpc_function_name[81];
char            override[256];

char            temp_buffer[1024];
char            file_buffer[1024];
char            error_buffer[1024];
char            chunk_buffer[4096];
char            proxy_buffer[1024];

int             start_of_last_command;
int             start_of_this_command;

int             prefer_remote_user = TRUE;
int             cookie_read_successfully;

/* Optional Google Sign-In configuration. Read from cgijacl.conf and
 * passed to auth_configure() once the file has been parsed. Empty
 * client_id or session_secret => auth disabled, anonymous flow only. */
char            google_client_id_cfg[256] = "\0";
char            session_secret_cfg[256] = "\0";
long            session_max_age_cfg = 30L * 24L * 3600L;

/* Set when auth_verify_session_cookie / auth_verify_id_token resolves
 * the request to a Google account. Used to gate Set-Cookie issuance
 * and to seed user_id for save-file resolution. */
char            google_sub[AUTH_SUB_MAX] = "\0";

/* Pending Set-Cookie value after a successful ?auth=google callback,
 * written out alongside the normal response headers further below. */
char            pending_session_cookie[AUTH_COOKIE_MAX] = "\0";
int             pending_clear_session_cookie = FALSE;

int             buffer_index = 0;

int             noun[4];
int             player = 0;

int             variable_contents;
int             oec;
int            *object_element_address,
               *object_backup_address;


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
llist           jacl_cookies;
struct stat     gamestat;

char            oops_buffer[1024];
char            oopsed_current[1024];
char            last_command[1024];
char            current_command[1024];

int             objects, integers, functions, strings;

extern struct stack_type backup[STACK_SIZE];
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
static void read_config_file(void);
static void word_check(void);

int
main(int argc, char *argv[])
{
    int             index;
    char           *last_slash;
    short int       returning_player = FALSE;

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

#ifdef _WIN32
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
        if (temp_buffer[index] == '/')    /* THERE IS NO SUFFIX */
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

    if (mkdir(temp_directory, 0755) != 0 && errno != EEXIST) {
        sprintf(error_buffer, "Cannot create temp directory \"%s\": %s",
                temp_directory, strerror(errno));
        log_error(error_buffer, PLUS_STDERR);
        terminate(42);
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

    if (read_gamefile()) {
        printf("Content-type: text/html\r\n\r\n");
        printf("<html><head><title>Error</title></head><body>");
        printf("<h1>Game Load Error</h1>");
        printf("<p>The game file contains errors and could not be loaded. ");
        printf("Please check the error log for details.</p>");
        printf("</body></html>");
        terminate(48);
    }

    // INTIALISE THE CSV PARSER
    csv_init(&parser_csv, CSV_APPEND_NULL);

    // CODE THAT IS ONLY RUN ONCE WHEN THE GAME IS LOADED
    execute ("+bootstrap");

    /* SAVE THE GAME STATE RIGHT AT THE START TO RESTORE FOR
       NEW USERS INSTEAD OF RESTARTING EVERY TIME */
    sprintf(saved_start, "%s%s-start.saved", temp_directory, prefix);

    if (save_game(saved_start) == FALSE) {
        /* Treat CANT_SAVE as a plain message, not a format string -- a
         * game that overrides it with a "%n" or unmatched-arg cstring
         * would otherwise hit a format-string vuln. Precision specifiers
         * cap each piece (cstrings are up to 1024 bytes, saved_start
         * is up to 256) so the formatted output fits error_buffer
         * without GCC -Wformat-truncation warnings. The filename info
         * is appended as a separate, engine-controlled line. */
        snprintf(error_buffer, sizeof error_buffer, "%.700s [%.255s]",
                 cstring_resolve("CANT_SAVE")->value, saved_start);
        log_error(error_buffer, PLUS_STDERR);
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

        /* DETERMINE FILE MODIFICATION TIME.  Take the newest mtime across
         * the game file and every shared include, so a git pull that only
         * touches a .library invalidates the preprocessed cache even when
         * the game's own .jacl is unchanged. */
        stat(argv[1], &gamefilestat);
        current_last_modified = gamefilestat.st_mtime;
        {
            time_t inc_mtime = newest_include_mtime();
            if (inc_mtime > current_last_modified) {
                current_last_modified = inc_mtime;
            }
        }

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

            /* Re-save saved_start so it matches the freshly loaded
             * object/integer/function/string counts. Without this,
             * the next fresh-user request's restore_game(saved_start)
             * sees the pre-reload count header and aborts with
             * "incompatible saved-game file", falling all the way
             * back to restart_game() again -- wasted work, plus the
             * misleading log noise. */
            if (save_game(saved_start) == FALSE) {
                snprintf(error_buffer, sizeof error_buffer, "%.700s [%.255s]",
                         cstring_resolve("CANT_SAVE")->value, saved_start);
                log_error(error_buffer, PLUS_STDERR);
            }

            previous_last_modified = current_last_modified;
        }
#endif

        user_id[0] = (char) 0;             // CLEAR THE USER_ID
        rpc_function_name[0] = (char) 0;    // CLEAR THE RPC FUNCTION NAME
        google_sub[0] = (char) 0;
        pending_session_cookie[0] = (char) 0;
        pending_clear_session_cookie = FALSE;

        read_cgi_input(&entries);
        parse_cookies(&jacl_cookies);
        cookie_read_successfully = FALSE;

        /* ?auth=google&credential=<id_token> -- frontend posts the
         * Google ID token here after the GIS button callback fires.
         * Verify it, build a signed session cookie, and short-circuit
         * the response with a tiny JSON body so the frontend can
         * reload and pick up the cookie. */
        if (auth_is_enabled() && cgi_val(entries, "auth") != NULL) {
            const char *auth_action = cgi_val(entries, "auth");
            if (!strcmp(auth_action, "google")) {
                const char *credential = cgi_val(entries, "credential");
                char sub[AUTH_SUB_MAX];
                char err[128];
                if (credential != NULL &&
                    auth_verify_id_token(credential, sub, sizeof(sub),
                                         err, sizeof(err)) == 0 &&
                    auth_make_session_cookie(sub, pending_session_cookie,
                                             sizeof(pending_session_cookie)) == 0) {
                    /* If the player was anonymous up to this point, carry
                     * their auto-continue file across to the signed-in
                     * slot so the act of logging in doesn't reset the
                     * game. Only migrate when the google slot is empty,
                     * so a first-time sign-in on a different browser
                     * never overwrites real signed-in progress. Manual
                     * saves (bookmark and named) are intentionally left
                     * behind under the anonymous user_id. */
                    {
                        const char *prior_uid =
                            cgi_val(jacl_cookies, "user_id");
                        if (prior_uid != NULL && prior_uid[0] != 0 &&
                            strncmp(prior_uid, "google_", 7) != 0) {
                            char old_auto[1024];
                            char new_auto[1024];
                            struct stat sb;
                            snprintf(old_auto, sizeof(old_auto),
                                     "%s%s-%s.auto",
                                     temp_directory, prefix, prior_uid);
                            snprintf(new_auto, sizeof(new_auto),
                                     "%s%s-google_%s.auto",
                                     temp_directory, prefix, sub);
                            if (stat(new_auto, &sb) != 0 &&
                                stat(old_auto, &sb) == 0 &&
                                rename(old_auto, new_auto) == 0) {
                                snprintf(error_buffer,
                                         sizeof(error_buffer),
                                         "Migrated anonymous auto-save "
                                         "%.480s -> %.480s on first sign-in",
                                         old_auto, new_auto);
                                log_error(error_buffer, LOG_ONLY);
                            }
                        }
                    }
                    const char *https_env = getenv("HTTPS");
                    const char *secure_flag =
                        (https_env != NULL && !strcasecmp(https_env, "on"))
                        ? " Secure;" : "";
                    printf("Status: 200 OK\r\n");
                    printf("Content-type: application/json\r\n");
                    /* Mark the auth response uncacheable. The body
                     * carries a Set-Cookie that's specific to this
                     * one sign-in; a misbehaving intermediary that
                     * cached the response would hand another user
                     * that cookie. */
                    printf("Cache-Control: no-store, private\r\n");
                    printf("Set-Cookie: jacl_session=%s; Path=/; HttpOnly;%s "
                           "SameSite=Lax; Max-Age=%ld\r\n",
                           pending_session_cookie, secure_flag,
                           auth_session_max_age());
                    printf("\r\n{\"ok\":true,\"sub\":\"%s\"}\n", sub);
                } else {
                    snprintf(error_buffer, sizeof error_buffer,
                             "Google ID token verify failed: %s",
                             credential ? err : "no credential");
                    log_error(error_buffer, LOG_ONLY);
                    printf("Status: 401 Unauthorized\r\n");
                    printf("Content-type: application/json\r\n");
                    printf("Cache-Control: no-store, private\r\n\r\n");
                    printf("{\"ok\":false}\n");
                }
                list_clear(&entries);
                continue;
            } else if (!strcmp(auth_action, "logout")) {
                const char *https_env = getenv("HTTPS");
                const char *secure_flag =
                    (https_env != NULL && !strcasecmp(https_env, "on"))
                    ? " Secure;" : "";
                printf("Status: 200 OK\r\n");
                printf("Content-type: application/json\r\n");
                printf("Cache-Control: no-store, private\r\n");
                printf("Set-Cookie: jacl_session=; Path=/; HttpOnly;%s "
                       "SameSite=Lax; Max-Age=0\r\n", secure_flag);
                printf("\r\n{\"ok\":true}\n");
                list_clear(&entries);
                continue;
            }
        }

        /* If a valid jacl_session cookie is present, the user_id for
         * this request is "google_<sub>". Falls through to the
         * existing anonymous flow on missing/invalid cookie so guests
         * still work. */
        if (auth_is_enabled()) {
            const char *session = cgi_val(jacl_cookies, "jacl_session");
            if (session != NULL &&
                auth_verify_session_cookie(session, google_sub,
                                           sizeof(google_sub)) == 0) {
                /* google_sub comes out of a JWT verified against
                 * Google's JWKS, so its charset should already be
                 * Google's stable [0-9A-Za-z._-]. Re-validate the
                 * composed user_id anyway -- it lands in file paths
                 * and Set-Cookie headers, and the cost is trivial
                 * compared to the blast radius if a future change
                 * loosens the upstream check. */
                snprintf(user_id, sizeof(user_id), "google_%s", google_sub);
                if (is_safe_user_id(user_id, sizeof(user_id))) {
                    cookie_read_successfully = TRUE;
                    returning_player = TRUE;
                    REMOTE_USER_USED->value = TRUE;
                } else {
                    log_error("Rejected Google session: composed user_id failed safe-charset check.",
                              LOG_ONLY);
                    user_id[0] = 0;
                    google_sub[0] = 0;
                }
            }
        }

        //sprintf (error_buffer, "HTTP_COOKIE: %s", getenv("HTTP_COOKIE"));
        //log_message(error_buffer, PLUS_STDERR);

        //sprintf (error_buffer, "user_id value pulled from cookies: %s", cgi_val(jacl_cookies, "user_id"));
        //log_message(error_buffer, PLUS_STDERR);

        /* Every code path that assigns user_id below routes through
         * is_safe_user_id(). The id ends up in a file path (auto-save)
         * and an HTTP Set-Cookie header, so a value containing '/',
         * CR/LF, or '..' would yield path traversal or response
         * splitting. Invalid candidates fall through to fresh
         * anonymous-id creation. */
        if (google_sub[0] != 0) {
            /* user_id already resolved from jacl_session above; the
             * existing user_id-cookie / parameter / REMOTE_USER /
             * generate-random fallback chain is skipped so a Google
             * sign-in always wins over a stale anonymous cookie. */
        } else if (cgi_val(entries, "user_id") != NULL || cgi_val(jacl_cookies, "user_id") != NULL) {
            // A user_id HAS BEEN PASSED TO THIS REQUEST VIA A PARMETER OR A COOKIE
            const char *candidate = NULL;
            int from_cookie = FALSE;
            if (prefer_remote_user == TRUE && REMOTE_USER != NULL && strcmp("", REMOTE_USER)) {
                /* PREFER REMOTE_USER FOR POTENTIAL SECURE SITES. */
                candidate = REMOTE_USER;
            } else if (cgi_val(jacl_cookies, "user_id") != NULL) {
                candidate = cgi_val(jacl_cookies, "user_id");
                from_cookie = TRUE;
            } else {
                candidate = cgi_val(entries, "user_id");
            }
            if (is_safe_user_id(candidate, sizeof(user_id))) {
                strcpy(user_id, candidate);
                REMOTE_USER_USED->value = (from_cookie || candidate == REMOTE_USER) ? TRUE : FALSE;
                cookie_read_successfully = from_cookie ? TRUE : cookie_read_successfully;
                returning_player = TRUE;
            } else {
                sprintf(error_buffer,
                        "Rejected unsafe user_id (length or charset); using a fresh anonymous id.");
                log_error(error_buffer, LOG_ONLY);
                /* Leave user_id untouched; the else-branch below will
                 * generate a secure random id. */
            }
        } else if (REMOTE_USER != NULL && strcmp("", REMOTE_USER)) {
            // REMOTE_USER IS SET AND user_id IS NULL SO USE REMOTE_USER
            if (is_safe_user_id(REMOTE_USER, sizeof(user_id))) {
                strcpy(user_id, REMOTE_USER);
                REMOTE_USER_USED->value = TRUE;
                returning_player = TRUE;
            } else {
                sprintf(error_buffer,
                        "Rejected unsafe REMOTE_USER value; using a fresh anonymous id.");
                log_error(error_buffer, LOG_ONLY);
            }
        }
        if (google_sub[0] == 0 && user_id[0] == 0) {
            // No usable user_id yet -- create a secure anonymous one.
            if (secure_random_user_id(user_id, sizeof(user_id)) != 0) {
                /* /dev/urandom unavailable -- refuse rather than fall
                 * back to rand() which gave guessable ~32k entropy
                 * seeded from request time. */
                log_error("Unable to read /dev/urandom for anonymous user_id; aborting request.",
                          PLUS_STDOUT);
                list_clear(&entries);
                continue;
            }

            // THIS IS THE FIRST COMMAND OF A NEW GAME
            returning_player = FALSE;

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

        /* Surface the auth state to game code. google_client_id is
         * populated even on anonymous requests so the front-end can
         * still render the Sign-In button; google_signed_in /
         * google_sub are zeroed unless this request is bound to a
         * verified Google session. */
        {
            struct string_type *cid = cstring_resolve("google_client_id");
            struct string_type *sub_str = cstring_resolve("google_sub");
            struct cinteger_type *signed_in = cinteger_resolve("google_signed_in");
            if (cid != NULL) {
                strncpy(cid->value, auth_google_client_id(),
                        sizeof(cid->value) - 1);
                cid->value[sizeof(cid->value) - 1] = 0;
            }
            if (sub_str != NULL) {
                strncpy(sub_str->value, google_sub,
                        sizeof(sub_str->value) - 1);
                sub_str->value[sizeof(sub_str->value) - 1] = 0;
            }
            if (signed_in != NULL) {
                signed_in->value = google_sub[0] != 0;
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
        web_status_emit_count = 0;

        /* If the JS sent a measured 'status_cols' (the actual character
         * width of the rendered #statuswin element on the client), use
         * it as the grid width for this request so cursor X Y +
         * +print_right land at the viewport's right edge. Without this
         * the grid stays at status_window_width's default and the
         * status text floats wherever that lands in pixel space. */
        {
            const char *scols = cgi_val(entries, "status_cols");
            if (scols != NULL && scols[0] != 0) {
                int v = atoi(scols);
                if (v >= 40 && v <= 500) {
                    struct integer_type *sww =
                        integer_resolve("status_window_width");
                    if (sww != NULL) sww->value = v;
                }
            }
        }

        /* DISPLAY THE HEADER OF THE HTML PAGE */
        if (cgi_val(entries, "rpc") == NULL && 
            (cgi_val(entries, "ajax") == NULL || strcmp(cgi_val(entries, "ajax"), "true"))) {
            if (execute("+header") == FALSE) {
                default_header();
            }
        }

        /* Detect a returning player whose auto-save was written mid-intro
         * (TOTAL_MOVES still 0). The restored state may include a
         * PENDING_QUESTION_TYPE from a getyesorno that the player never
         * answered before closing the browser. With pending state set,
         * the engine would treat the empty initial-GET input as the
         * answer, write only "Please enter yes or no", and never re-show
         * the question text. Clear the pending fields and let +intro re-
         * run with its state guards so the unanswered question (and only
         * that question) gets re-emitted. */
        if (returning_player == TRUE
            && TOTAL_MOVES != NULL && TOTAL_MOVES->value <= 0
            && (cgi_val(entries, "command") == NULL)
            && (cgi_val(entries, "rpc") == NULL)) {
            struct integer_type *ptype = PENDING_QUESTION_TYPE;
            if (ptype != NULL && ptype->value != 0) {
                struct integer_type *plow = PENDING_NUMBER_LOW;
                struct integer_type *phigh = PENDING_NUMBER_HIGH;
                struct string_type *ptarget =
                    string_resolve("pending_target");
                ptype->value = 0;
                if (plow != NULL) plow->value = 0;
                if (phigh != NULL) phigh->value = 0;
                if (ptarget != NULL) ptarget->value[0] = 0;
            }
            returning_player = FALSE;
        }

        if (returning_player == FALSE
            && (cgi_val(entries, "command") == NULL)
            && (cgi_val(entries, "rpc") == NULL)) {
            /* THIS IS THE START OF A NEW GAME, NOT AN EXISTING USER
             * CONTINUING A GAME */

            /* DISPLAY THE GAMES INTRODUCTION AS THIS IS THE FIRST MOVE */
            execute("+intro");

            /* IF THERE IS A PENDING QUESTION FROM THE INTRO, SKIP
             * THE INITIAL GAME STATE CHECK AND EACHTURN - THESE WILL
             * RUN AFTER THE PLAYER ANSWERS THE QUESTION */
            {
                struct integer_type *ptype = PENDING_QUESTION_TYPE;
                if (ptype == NULL || ptype->value == 0) {
                    /* DUMMY RETRIEVE OF 'HERE' FOR TESTING OF GAME STATE */
                    get_here();
                    /* TIME PASSES BEFORE THE PLAYER SEES THE FIRST PROMPT */
                    eachturn();
                }
            }

        } else {

            /* THIS MOVE IS A CONTINUATION OF A GAME THAT IS IN PROGRESS */

            if (returning_player == FALSE) {
                // SETUP THE GAME STATE IF THERE IS A COMMAND 
                // BUT IT IS NOT A RETURNING PLAYER
                execute("+setup");
            }

            TIME->value = TRUE;
            custom_error = FALSE;

            /* PUT DIRECT FUNCTION CALL CHECKS HERE */
            if (cgi_val(entries, "rpc") != NULL) {
                // ALL FUNCTION CALLS ARE AJAX RPC BY DEFINITION

                if (!strcmp(cgi_val(entries, "rpc"), "timer")) {
                    execute("+timer");
                } else if (!strcmp(cgi_val(entries, "rpc"), "resize")) {
                    /* Client viewport resized: re-render the status
                     * bar at the new column count (already applied to
                     * status_window_width from &status_cols= above).
                     * No turn state mutation -- just the bar. */
                    web_render_status_bar();
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
                    /* snprintf bounds the combined "+" + rpc-arg copy
                     * to rpc_function_name's full capacity. The prior
                     * strcpy("+") + strncat(..., 80) wrote up to 82
                     * bytes into the 81-byte buffer when the HTTP
                     * ?rpc= value reached 80 chars. */
                    snprintf(rpc_function_name, sizeof rpc_function_name,
                             "+%s", cgi_val(entries, "rpc"));
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

                /* CHECK IF THERE IS A PENDING QUESTION FROM A PREVIOUS
                 * GETYESORNO OR GETNUMBER COMMAND.
                 * THE RESTART COMMAND ALWAYS BYPASSES THE PENDING CHECK */
                {
                    struct integer_type *ptype = PENDING_QUESTION_TYPE;

                    if (ptype != NULL && ptype->value != 0
                        && strcasecmp(text_buffer, cstring_resolve("RESTART_WORD")->value)) {
                        int question_type = ptype->value;
                        int answer_valid = FALSE;
                        int answer_value = 0;

                        if (question_type == 1) {
                            /* GETYESORNO */
                            int result = resolve_yes_or_no(text_buffer);
                            if (result != -1) {
                                answer_value = result;
                                answer_valid = TRUE;
                            } else {
                                /* Use the language library's YES_OR_NO
                                 * constant so the prompt is in the
                                 * game's language (english, indonesian,
                                 * french, german, spanish all define
                                 * it). */
                                write_text(cstring_resolve("YES_OR_NO")->value);
                            }
                        } else if (question_type == 2 || question_type == 3) {
                            /* GETNUMBER (2=insistent) OR ASKNUMBER (3=non-insistent) */
                            struct integer_type *plow = PENDING_NUMBER_LOW;
                            struct integer_type *phigh = PENDING_NUMBER_HIGH;
                            char *endptr;
                            long val;

                            if (plow != NULL && phigh != NULL) {
                                val = strtol(text_buffer, &endptr, 10);
                                /* CHECK IF INPUT IS A VALID NUMBER */
                                while (*endptr == ' ' || *endptr == '\t' || *endptr == '\n') endptr++;
                                if (*endptr == '\0' && endptr != text_buffer) {
                                    if (val >= plow->value && val <= phigh->value) {
                                        answer_value = (int)val;
                                        answer_valid = TRUE;
                                    } else if (question_type == 3) {
                                        /* ASKNUMBER: NON-INSISTENT, ACCEPT ANYWAY */
                                        answer_value = (int)val;
                                        answer_valid = TRUE;
                                    } else {
                                        sprintf(temp_buffer, "Please enter a number between %d and %d.^", plow->value, phigh->value);
                                        write_text(temp_buffer);
                                    }
                                } else if (question_type == 3) {
                                    /* ASKNUMBER: NON-INSISTENT, ACCEPT 0 */
                                    answer_value = 0;
                                    answer_valid = TRUE;
                                } else {
                                    sprintf(temp_buffer, "Please enter a number between %d and %d.^", plow->value, phigh->value);
                                    write_text(temp_buffer);
                                }
                            }
                        } else if (question_type == 4 || question_type == 5) {
                            /* GETSTRING (4=insistent) OR ASKSTRING (5=non-insistent) */
                            /* STRIP LEADING/TRAILING WHITESPACE */
                            char *s = text_buffer;
                            int len;
                            while (*s == ' ' || *s == '\t') s++;
                            len = strlen(s);
                            while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' || s[len-1] == '\n' || s[len-1] == '\r'))
                                len--;

                            if (len > 0 || question_type == 5) {
                                /* STORE DIRECTLY INTO THE TARGET STRING */
                                struct string_type *ptarget = string_resolve("pending_target");
                                if (ptarget != NULL && ptarget->value[0] != 0) {
                                    struct string_type *target_str = string_resolve(ptarget->value);
                                    if (target_str != NULL) {
                                        strncpy(target_str->value, s, len < 1023 ? len : 1023);
                                        target_str->value[len < 1023 ? len : 1023] = 0;
                                    }
                                }
                                answer_valid = TRUE;
                            } else {
                                write_text("Please enter a response.^");
                            }
                        }

                        if (answer_valid) {
                            /* STORE THE ANSWER IN THE TARGET VARIABLE (for integer types) */
                            if (question_type <= 3) {
                                struct string_type *ptarget = string_resolve("pending_target");
                                if (ptarget != NULL && ptarget->value[0] != 0) {
                                    int *target = container_resolve(ptarget->value);
                                    if (target != NULL) {
                                        *target = answer_value;
                                    }
                                }
                            }

                            /* CLEAR PENDING STATE */
                            ptype->value = 0;
                            {
                                struct integer_type *plow = PENDING_NUMBER_LOW;
                                struct integer_type *phigh = PENDING_NUMBER_HIGH;
                                struct string_type *ptarget2 = string_resolve("pending_target");
                                if (plow != NULL) plow->value = 0;
                                if (phigh != NULL) phigh->value = 0;
                                if (ptarget2 != NULL) ptarget2->value[0] = 0;
                            }

                            TIME->value = TRUE;
                            /* If the pending question came from +intro
                             * (no real moves yet), bump the intro_answers
                             * counter and give +intro another go instead
                             * of running eachturn -- this lets a multi-
                             * question intro chain getyesorno calls
                             * across requests. The JACL state guards key
                             * off intro_answers to know which question is
                             * up. Returning_player is FALSE for fresh
                             * incognito sessions and TRUE for anyone with
                             * cookies; both cases need the intro re-run
                             * so we only condition on TOTAL_MOVES. */
                            if (TOTAL_MOVES != NULL
                                && TOTAL_MOVES->value <= 0) {
                                struct integer_type *answers =
                                    integer_resolve("intro_answers");
                                if (answers != NULL) {
                                    answers->value++;
                                }
                                execute("+intro");
                                /* If +intro fully resolved (no further
                                 * pending question), do the same post-
                                 * intro tick the fresh-game path would
                                 * have done -- get_here() + eachturn().
                                 * Without this TOTAL_MOVES stays at -1,
                                 * the status bar shows 'Moves: -1', and
                                 * +eachturn (which sets up first-turn
                                 * NPC positions etc.) never runs. */
                                {
                                    struct integer_type *p =
                                        PENDING_QUESTION_TYPE;
                                    if (p == NULL || p->value == 0) {
                                        get_here();
                                        eachturn();
                                    }
                                }
                            } else {
                                /* RUN EACHTURN SO THE GAME CAN REACT TO THE
                                 * ANSWER. USE THE C eachturn() FUNCTION, NOT
                                 * execute("+eachturn"), BECAUSE eachturn()
                                 * INCREMENTS total_moves FIRST */
                                eachturn();
                            }
                        } else if (question_type == 1
                                   && TOTAL_MOVES != NULL
                                   && TOTAL_MOVES->value <= 0) {
                            /* Invalid yes/no answer during the pre-game
                             * intro flow. The "Please enter yes or no"
                             * hint has been written above; re-run +intro
                             * so the game's state-guarded question text
                             * is re-emitted, restoring context for the
                             * player who's now staring at just the hint. */
                            execute("+intro");
                        }
                        /* SKIP NORMAL COMMAND PROCESSING WHEN PENDING */
                        goto skip_command;
                    }
                }

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
skip_command:

                if (current_command[0] != 0) {
                    strcpy(last_command, current_command);
                }

                /* Returning player with no command -- a page reload mid-
                 * game. Nothing in the player's saved state advanced, so
                 * eachturn() above wasn't called (the synthetic
                 * "blankjacl" verb sets TIME=false). Fire +eachturn
                 * explicitly so games can resync session-bound state
                 * (ambient sound, music channels, periodic effects) that
                 * the browser lost on reload. Skips total_moves increment
                 * because no real action was taken. */
                if (returning_player
                    && cgi_val(entries, "command") == NULL
                    && cgi_val(entries, "rpc") == NULL) {
                    execute("+eachturn");
                }
            }
        }

        /* If the command processed this request didn't call updatestatus
         * (e.g. look/inventory/examine -- verbs that set TIME=false and
         * skip +system_eachturn), emit one now. Keeps the status bar
         * grid in sync with the latest &status_cols= even on non-turn
         * commands, so a browser resize followed by any command refreshes
         * the bar's width. Skipped for full-page (non-ajax) loads, which
         * already render the bar via the +intro path. */
        if (web_status_emit_count == 0
            && cgi_val(entries, "ajax") != NULL
            && !strcmp(cgi_val(entries, "ajax"), "true")) {
            web_render_status_bar();
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
            /* See the earlier CANT_SAVE site for the rationale: treat
             * the game-defined cstring as a plain message, append the
             * filename info via the engine-controlled "%s [%s/%s]"
             * template. */
            snprintf(error_buffer, sizeof error_buffer,
                     "%.600s [%.80s/%.255s]",
                     cstring_resolve("CANT_SAVE")->value, prefix, temp_buffer);
            log_error(error_buffer, PLUS_STDOUT);
        }

        list_clear(&entries);

    }

    return (1);
}

void
default_header()
{
    /* THIS HEADER IS DISPLAYED IF NO CUSTOM ONE IS PROVIDED */

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
    /* THIS FOOTER IS DISPLAYED IF NO CUSTOM ONE IS PROVIDED */

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
    int    *container;
    struct string_type *string = NULL;

    if (pointer == NULL)
        return;

    // LOOP THROUGH ALL THE DEFINED PARAMETERS
    do { 
        // DOES    THE CURRENT PARAMETER HAVE A VALUE PASSED WITH THIS REQUEST?
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
        /* CLEAR ANY PENDING QUESTION STATE BEFORE RESTARTING */
        {
            struct integer_type *ptype = PENDING_QUESTION_TYPE;
            if (ptype != NULL) ptype->value = 0;
        }

        if (execute("+restart_game") == FALSE) {
            TIME->value = TRUE;
            if (restore_game(saved_start, FALSE) == FALSE) {
                restart_game();
            }

            execute("+intro");
            {
                struct integer_type *ptype = PENDING_QUESTION_TYPE;
                if (ptype == NULL || ptype->value == 0) {
                    eachturn();
                }
            }
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
    /*
     * Quit is intentionally NOT intercepted here. Under cgijacl it used to
     * call terminate(0), which exits the request mid-response and the
     * browser sees a broken/blank page. Under fcgijacl it was never
     * intercepted at all -- the word falls through to the parser and the
     * game's grammar decides what to do (e.g. `keluar` -> +out in the
     * Indonesian library). Passing through keeps cgijacl and fcgijacl
     * consistent and avoids the crash.
     */
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
    /* Drive the loop off fgets's return rather than feof so a read
     * error or EOF doesn't cause the last line to be re-encapsulated. */
    while (fgets(text_buffer, 1024, file) != NULL) {
        encapsulate();

        /* Each config directive that copies word[1] into a fixed-size
         * buffer uses snprintf here so the destination is bounded AND
         * guaranteed NUL-terminated. The previous strncpy(dst, src, 80)
         * left the trailing byte uninitialized when src was >= 80 chars,
         * and the strcat() calls a few lines below would walk off the
         * end looking for a NUL. */
        if (word[0] == NULL) {
            // DO NOTHING
        } else if (!strcmp(word[0], "temp")) {
            if (word[1] != NULL) {
                snprintf(temp_directory, sizeof(temp_directory), "%s", word[1]);
                if (temp_directory[strlen(temp_directory) - 1] != '/')
                    strcat(temp_directory, "/");
            }
        } else if (!strcmp(word[0], "prefer_remote_user")) {
            prefer_remote_user = TRUE;
        } else if (!strcmp(word[0], "ignore_remote_user")) {
            prefer_remote_user = FALSE;
        } else if (!strcmp(word[0], "data")) {
            if (word[1] != NULL) {
                snprintf(data_directory, sizeof(data_directory), "%s", word[1]);
                if (data_directory[strlen(data_directory) - 1] != '/')
                    strcat(data_directory, "/");
            }
        } else if (!strcmp(word[0], "include")) {
            if (word[1] != NULL) {
                snprintf(include_directory, sizeof(include_directory), "%s", word[1]);
                if (include_directory[strlen(include_directory) - 1] != '/')
                    strcat(include_directory, "/");
            }
        } else if (!strcmp(word[0], "error_log")) {
            if (word[1] != NULL) {
                snprintf(error_log, sizeof(error_log), "%s", word[1]);
                if (error_log[strlen(error_log) - 1] == '/')
                    strcat(error_log, "error.log");
            }
        } else if (!strcmp(word[0], "access_log")) {
            if (word[1] != NULL) {
                snprintf(access_log, sizeof(access_log), "%s", word[1]);
                if (access_log[strlen(access_log) - 1] == '/')
                    strcat(access_log, "access.log");
            }
        } else if (!strcmp(word[0], "cookie_expiry")) {
            if (word[1] != NULL) {
                snprintf(cookie_expiry, sizeof(cookie_expiry), "%s", word[1]);
            }
        } else if (!strcmp(word[0], "google_client_id")) {
            if (word[1] != NULL) {
                strncpy(google_client_id_cfg, word[1],
                        sizeof(google_client_id_cfg) - 1);
                google_client_id_cfg[sizeof(google_client_id_cfg) - 1] = 0;
            }
        } else if (!strcmp(word[0], "session_secret")) {
            if (word[1] != NULL) {
                strncpy(session_secret_cfg, word[1],
                        sizeof(session_secret_cfg) - 1);
                session_secret_cfg[sizeof(session_secret_cfg) - 1] = 0;
            }
        } else if (!strcmp(word[0], "session_max_age")) {
            if (word[1] != NULL) {
                session_max_age_cfg = strtol(word[1], NULL, 10);
            }
        }
    }

    fclose(file);
    file = NULL;

    auth_configure(google_client_id_cfg,
                   session_secret_cfg,
                   session_max_age_cfg);
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

/* ----- Web status window grid ------------------------------------------
 * Mimics a GLK TextGrid window for the web build so games can use the
 * same cursor X Y + write pattern in +update_status_window_web that they
 * use in +update_status_window for GLK. The grid is filled with spaces,
 * cursor commands move the cursor, and write_text below diverts output
 * into the grid (one char per cursor position) when web_status_active().
 * web_status_end emits the buffered grid as <br>-separated rows back
 * through write_text -- now in normal mode -- so the rest of the
 * <jacl-status> machinery in the JS picks it up. */

#define WEB_STATUS_MAX_ROWS 32
/* Must be >= the &status_cols= upper bound clamped in the request handler
 * (currently 500). web_render_status_bar right-aligns score/moves at column
 * status_window_width - strlen - 1; if that column exceeds the grid width,
 * web_status_putchar bounds-checks x and silently drops the chars, so on a
 * wide viewport "Score: N  Moves: N" lands past the old 200-col cap and
 * vanishes off the right edge entirely. */
#define WEB_STATUS_MAX_COLS 512

static int  web_status_mode = 0;
static int  web_status_x = 0;
static int  web_status_y = 0;
static int  web_status_w = 80;
static int  web_status_h = 1;
/* Counter incremented each time web_render_status_bar() (defined in
 * interpreter.c) runs. The request handler resets it to 0 at the
 * start of each request and checks it at the end -- if still 0 (no
 * updatestatus fired during the command, e.g. a TIME=false verb
 * like look/inventory) it emits one anyway so the bar always tracks
 * the latest grid width sent via &status_cols=. */
int         web_status_emit_count = 0;
static char web_status_grid[WEB_STATUS_MAX_ROWS][WEB_STATUS_MAX_COLS + 1];

void
web_status_begin(int rows, int cols)
{
    int i, j;
    if (rows < 1) rows = 1;
    if (rows > WEB_STATUS_MAX_ROWS) rows = WEB_STATUS_MAX_ROWS;
    if (cols < 1) cols = 1;
    if (cols > WEB_STATUS_MAX_COLS) cols = WEB_STATUS_MAX_COLS;
    web_status_h = rows;
    web_status_w = cols;
    web_status_x = 0;
    web_status_y = 0;
    for (i = 0; i < web_status_h; i++) {
        for (j = 0; j < web_status_w; j++) {
            web_status_grid[i][j] = ' ';
        }
        web_status_grid[i][web_status_w] = 0;
    }
    web_status_mode = 1;
}

void
web_status_cursor(int x, int y)
{
    web_status_x = x;
    web_status_y = y;
}

void
web_status_putchar(int c)
{
    if (c == '^' || c == '\n') {
        web_status_y++;
        web_status_x = 0;
        return;
    }
    if (web_status_y < 0 || web_status_y >= web_status_h) return;
    if (web_status_x < 0 || web_status_x >= web_status_w) return;
    web_status_grid[web_status_y][web_status_x] = (char) c;
    web_status_x++;
}

int
web_status_active(void)
{
    return web_status_mode;
}

void
web_status_end(void)
{
    int i, j;
    char row_html[WEB_STATUS_MAX_COLS * 6 + 16];
    int p;

    /* Switch off status mode FIRST so the write_text calls below go
     * out as normal HTML rather than back into the grid. */
    web_status_mode = 0;

    for (i = 0; i < web_status_h; i++) {
        if (i > 0) write_text("<br>");
        p = 0;
        for (j = 0; j < web_status_w; j++) {
            char c = web_status_grid[i][j];
            if (c == '<') {
                strcpy(&row_html[p], "&lt;");
                p += 4;
            } else if (c == '>') {
                strcpy(&row_html[p], "&gt;");
                p += 4;
            } else if (c == '&') {
                strcpy(&row_html[p], "&amp;");
                p += 5;
            } else if (c == ' ') {
                /* Preserve runs of spaces -- white-space:pre on the
                 * #statuswin div handles this, but be defensive in
                 * case the HTML gets rendered somewhere else too. */
                strcpy(&row_html[p], "&nbsp;");
                p += 6;
            } else {
                row_html[p++] = c;
            }
        }
        row_html[p] = 0;
        write_text(row_html);
    }
}

/* web_render_status_bar() is defined in interpreter.c next to the
 * updatestatus opcode handler; the prototype lives in prototypes.h.
 * The call sites in this file go through that single definition. */

void
write_text(const char *tout_buffer)
{
    int             index;

    /* Status-grid mode: route every character into the virtual grid
     * at the current cursor position. ~ -> " conversion still
     * applies so games can use the same JACL string syntax as for
     * normal text. The grid emits itself as HTML in web_status_end. */
    if (web_status_active()) {
        for (index = 0; tout_buffer[index] != 0; index++) {
            int c = tout_buffer[index];
            if (c == '~') c = '"';
            web_status_putchar(c);
        }
        return;
    }

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
restore_interaction(const char *filename)
{

    if (filename == NULL) {
        sprintf(file_buffer, "%s%s-%s-bookmark", temp_directory, prefix, user_id);
    } else {
        const char *raw = text_of(filename);
        /* `raw` is game-supplied (the JACL author passes a noun or a
         * literal). Reject anything that could escape temp_directory:
         * slashes, backslashes, "..", control chars. */
        if (!is_safe_filename_component(raw, 81)) {
            write_text(cstring_resolve("CANT_RESTORE")->value);
            return (FALSE);
        }
        sprintf(file_buffer, "%s%s-%s-%s", temp_directory, prefix, user_id, raw);
    }

    if (restore_game(file_buffer, TRUE) == FALSE) {
        write_text(cstring_resolve("CANT_RESTORE")->value);
        return (FALSE);
    } else {
        return (TRUE);
    }
}

int
save_interaction(const char *filename)
{

    if (filename == NULL) {
        sprintf(file_buffer, "%s%s-%s-bookmark", temp_directory, prefix, user_id);
    } else {
        const char *raw = text_of(filename);
        if (!is_safe_filename_component(raw, 81)) {
            write_text(cstring_resolve("CANT_SAVE")->value);
            return (FALSE);
        }
        sprintf(file_buffer, "%s%s-%s-%s", temp_directory, prefix, user_id, raw);
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

