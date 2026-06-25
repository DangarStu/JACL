/* webjacl.h --- Header file for built-in JACL http server.
 * (C) 2001 Andreas Matthias, distribute and use 
 * according to GNU GPL, see file COPYING for details.
 */

#ifndef _WEBJACL_H
#define _WEBJACL_H

/* Is this a development or a production server?
   This option should really be set with a command
   line flag, though I think that perhaps a "development"
   server is not so unstable at all, and therefore for
   now we just set it as the default.

   The first difference is that a development server 
   sets the SO_REUSEADDR socket option, so that the user
   does not get "Address already in use" errors when
   he restarts the server immediately after stopping
   it. The drawback is that under some circumstances
   TCP packets from a previous connection, which are
   still floating around in the Net, may arrive after
   the socket has been reused and confuse the server.
   According to the "Unix Socket FAQ" 
   (http://www.ibrado.com/sock-faq/index.html)
   this scenario is possible but not probable, and so
   we just take the risk here. 
   
   The second diffrence is that a development server
   will look at the modification time of the game 
   file being served, and it will restart itself if
   the game file is modified. This makes development
   of games much easier, but will be slightly slower
   than a server which does not check the timestamp
   of the game file at each connection (that is, the
   production server).
   
   WJ_SERVERTYPE options: WJ_DEVSERVER, WJ_PRODSERVER
*/

#define WJ_SERVERTYPE WJ_DEVSERVER
/* #define WJ_SERVERTYPE WJ_PRODSERVER */

/* #define NATIVE_LANGUAGE GERMAN */
/* The default port to use if a -p command line option
   is not given at startup */

#define WJ_DEFAULT_PORT 4269

/* Optional network stuff. If you get compilation 
   problems on strange platforms (e.g. Windows) set
   this to 0. No essetial functionality is lost, just
   some cosmetic things may look differently.
   Recommended setting is 1 */

#define OPTIONAL_NET 1

/* ********** Nothing to edit after this point ************ */

/* Socket things */

#include <signal.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#ifdef NOZOMBIES
#include <sys/wait.h>
#endif

#ifdef _WIN32
/* Native Windows (mingw-w64) build: sockets are NOT file descriptors, so the
 * POSIX socket headers below do not apply. Pull in Winsock and the small-IO
 * helpers instead. All Windows-specific behaviour in webjacl.c is guarded by
 * #ifdef _WIN32 so the POSIX (macOS/Linux) build is byte-for-byte unchanged. */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#include <process.h>
#include <ctype.h>
#include <errno.h>

/* Winsock uses SOCKET (an unsigned handle), not int, and closesocket() rather
 * than close(). Map them so the shared code can keep using the POSIX names. */
typedef SOCKET wj_socket_t;
#define WJ_INVALID_SOCK INVALID_SOCKET
#ifndef close
#define close closesocket
#endif

/* Windows has no setenv(); _putenv_s has the same (name, value) effect. */
#ifndef setenv
#define setenv(n, v, o) _putenv_s((n), (v))
#endif

/* socklen_t is not always defined by the mingw headers. */
#ifndef socklen_t
typedef int socklen_t;
#endif

#else /* POSIX */
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>

typedef int wj_socket_t;
#define WJ_INVALID_SOCK (-1)
#endif /* _WIN32 */

#define DEFAULT_PROTOCOL 0

/* Server signature for some HTTP headers */
#define WJ_SERVERNAME "WebJACL built-in server v0.9"

/* Output buffer size (512k) */
#define WJ_BUFFERSIZE 512*1024

/* Maximum size of HTTP request that we accept (16k) */
#define WJ_MAX_REQ_SIZE 16*1024

/* Size of listen() backlog */
#define MAXUSER 15

/* Define the FCGI_ToFILE macro as nop. It is used by fcgi to convert an
   FCGI_FILE* (or something like that) back into a plain C FILE*. Since we
   don't use fcgi here, we don't need that conversion.
*/
#define FCGI_ToFILE(x) x

/* Rename wj_listen() to FCGI_Accept(), so that we can
   replace fcgi without source code changes in JACL.
   ("wj_" is short for "webjacl")
*/
#define FCGI_Accept wj_listen

/* Some symbolic constants for the return codes of wj_setup() */
#define WJ_OK 0
#define WJ_NOPORT 1

/* Standard file descriptor names. We don't want to
   include unistd.h (because jacl.c redefines truncate()),
   therefore we assign the constants here under new names,
   so that things don't mess up if someone later does
   manage to include unistd.h */
#define MYSTDIN  0
#define MYSTDOUT 1
#define MYSTDERR 2

/* Servertypes: development or production server */
#define WJ_DEVSERVER  0
#define WJ_PRODSERVER 1

/* Function prototypes */
int  wj_setup( int* pargc, char **pargv );
void wj_cleanup( int signo );
int  wj_listen( void );
int  wj_setupdb( void );
void wj_cleanup_without_exit( void );
void wj_connect_io( wj_socket_t clientFd );
void wj_disconnect_io( wj_socket_t clientFd );

#endif
