/*
 * webjacl.c --- Built-in JACL http server library routines. (C) 2001 Andreas
 * Matthias
 * 
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 1, or (at your option) any later
 * version.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 675
 * Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdio.h>
#include <stdlib.h>		/* for atoi() */
#include <time.h>
#include <sys/stat.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#include <fcntl.h>
#include <process.h>
#else
#if OPTIONAL_NET==1
#include <netdb.h>
#endif
#endif

#include "webjacl.h"
#include "language.h"

#ifdef _WIN32
/* On Windows a connected socket is a SOCKET handle, NOT a file descriptor, so
 * the POSIX trick of dup2()'ing it onto stdin/stdout does not work. Instead we
 * buffer the HTTP request and response through temp files: recv() the request
 * into a temp file that becomes stdin (so fgets(stdin) parses it unchanged),
 * collect the response in a temp file that becomes stdout (so the printf/fwrite
 * response code is unchanged), then send() that file's bytes to the socket when
 * the connection is torn down. These globals hold that per-connection state. */
static char wj_req_tmpname[MAX_PATH];
static char wj_resp_tmpname[MAX_PATH];
#endif

/* Saved original stdin/stdout file descriptors. On both platforms the per-
 * connection I/O helpers stash the real stdin/stdout here while stdin/stdout
 * are pointed at the client (POSIX: the socket itself; Windows: temp files)
 * and restore them when the connection is torn down. */
static int  wj_savefdIN  = -1;
static int  wj_savefdOUT = -1;

/* Some variables... */
/* Port number for webjacl server (is set on command line) */
int             wj_port = WJ_DEFAULT_PORT;

/* Hostname of this machine */
char            wj_hostname[4096];

/* acl game file name */
char            wj_script_name[4096], wj_script_dir[4096];

/* Server startup time. */
time_t          wj_startuptime;

/*
 * Development or production server? (this is set in webjacl.h)
 */
int             wj_servertype = WJ_SERVERTYPE;

/* Some file descriptor and socket things. wj_socket_t is int on POSIX (so this
 * is unchanged there) and SOCKET on Windows, where a connected socket is a
 * handle rather than a file descriptor. */
wj_socket_t     serverFd, clientFd;
struct sockaddr_in serverAddress;
struct sockaddr *serverSockAddrPtr;
struct sockaddr_in clientAddress;
struct sockaddr *clientSockAddrPtr;
int             serverLen, maxFd = 0;
socklen_t 		clientLen;
fd_set          rset;

/* External media list */
struct media {
	char            data[1024];
	struct media   *next;
};

struct media    mediadb;




/*
 * int wj_setup( int argc, char **argv ) This function must be called once at
 * server startup. It looks into argv[][] for a port command line parameter
 * "-p", e.g. -p8080 or -p 8080. If a port number can't be found, the
 * function returns an error (WJ_NOPORT), else it returns 0 (WJ_OK).
 * 
 * It sets the global variable wj_port to the given port number, the global
 * variable wj_hostname to our hostname (or "localhost" if gethostname()
 * can't determine the real host name) and the variable wj_script_name to the
 * path of the game .acl file, which always is the last parameter.
 * 
 * This function _modifies_ argc and argv (!) by removing the port parameter, so
 * that the JACL main() can stay unmodified and do its own command line
 * parsing.
 * 
 * The code for this does not look particularly inspired, but it works. Someone
 * take pity and do it better, perhaps with GNU getopt.
 */

int
wj_setup(int *pargc, char **pargv)
{
	int             index;
	int             i, j, port = 0;
	char            tmpname[256], *last_slash;
#if OPTIONAL_NET==1
	struct hostent *he_ptr;
#endif

	/*
	 * Before we start, set wj_script_name to the value of the last cmd
	 * line parameter
	 */
	strcpy(wj_script_name, pargv[*pargc - 1]);
	strcpy(tmpname, wj_script_name);

	for (index = 0; index < strlen(tmpname); index++) {
		if (tmpname[index] == '\\')
			tmpname[index] = '/';
	}

	/*
	 * Set wj_script_dir; we'll need this to serve external content
	 */
	last_slash = strrchr(tmpname, '/');
	if (last_slash != NULL) {
		*last_slash = '\0';
	} else
		/* We're in current directory */
		tmpname[0] = '\0';

	strcpy(wj_script_dir, tmpname);

	/*
	 * Determine our hostname. If gethostname() can't determine our
	 * hostname, we just set it to "localhost", hoping that in this case
	 * there aren't going to be any remote users anyway. I am not sure if
	 * this is a good idea...
	 */
	if (gethostname(wj_hostname, 4095) != 0)
		strcpy(wj_hostname, "localhost");

#if OPTIONAL_NET==1
	/*
	 * We want a fully qualified hostname (FQDN) if possible. So now try
	 * to get the DNS domain name.
	 */
	he_ptr = gethostbyname(wj_hostname);
	if (he_ptr != NULL)
		if (strlen(he_ptr->h_name) > 0)
			strcpy(wj_hostname, he_ptr->h_name);
	/* If we don't get an FQDN, just use the plain hostname */
#endif


	/* Remember the server startup time */
	wj_startuptime = time((time_t *) NULL);

	/* Setup the external media list */
	wj_setupdb();

	for (i = 1; i < *pargc; i++)
		if ((pargv[i][0] == '-') && (pargv[i][1] == 'p')) {
			if (pargv[i][2] == '\0') {
				/*
				 * Port number is in next parameter (if one
				 * exists)
				 */
				if (*pargc > i + 1)
					port = atoi(pargv[i + 1]);
				else
					return (WJ_NOPORT);

				if (port == 0)
					return (WJ_NOPORT);

				/*
				 * Now remove these 2 parameters from
				 * argc/argv
				 */
				for (j = i + 2; j < *pargc; j++)
					strcpy(pargv[j - 2], pargv[j]);
				*pargc -= 2;

				/* Store port in global variable */
				wj_port = port;
				return (WJ_OK);
			} else {
				/*
				 * Port number is in this string directly
				 * after "-p"
				 */
				port = atoi(&pargv[i][2]);
				if (port == 0)
					return (WJ_NOPORT);
				else {
					/*
					 * Now remove this parameter from
					 * argc/argv
					 */
					for (j = i + 1; j < *pargc; j++)
						strcpy(pargv[j - 1], pargv[j]);
					(*pargc)--;

					/* Store port in global variable */
					wj_port = port;
					return (WJ_OK);
				}
			}
		} else
			return (WJ_NOPORT);

	/*
	 * I think the following is never reached (?) but it doesn't hurt
	 * either
	 */

	/* If we have no cmd line parameters at all, complain */
	if (port == 0)
		return (WJ_NOPORT);

	/* Store port in global variable */
	wj_port = port;
	return (WJ_OK);
}


int
wj_setupdb(void)
{
	FILE           *fp;
	char            fname[8096];
	int             i = 0;
	int             index;
	struct media   *p = NULL;
	struct media   *oldp = NULL;

	/* Initialize anchor of media list */
	mediadb.data[0] = '\0';
	mediadb.next = p;
	oldp = &mediadb;

	/*
	 * First look for a media description file. It must be in the same
	 * directory as the game file and have the same core name with the
	 * suffix ".media"
	 */
	strcpy(fname, wj_script_name);
	for (index = strlen(fname); index >= 0; index--) {
		if (fname[index] == '/')
			break;
		if (fname[index] == '.') {
			fname[index] = 0;
			break;
		}
	}

	strcat(fname, ".media");
	if ((fp = fopen(fname, "r")) == NULL) {
		/* Media file does not exist. Inform the user */
		fprintf(stderr, NO_MEDIA,
			fname);

		/*
		 * We need not do anything else. If there was no media file,
		 * every attempt to access external media will simply fail
		 * with a 404 error.
		 */
	} else {
		/*
		 * We have a media file. Read it. The format is: <http
		 * request string> <MIME type> <path> Path is relative to the
		 * location of this file. Empty lines and "#"-comments
		 * allowed.
		 */
		int             fields;
		char           *terminator;
		/* Sized to match the sscanf width specs below so the compiler
		 * can prove the subsequent snprintf into p->data[1024] cannot
		 * truncate. 256 + 128 + 512 + 2 separators + NUL = 899 bytes. */
		char            sreq[256], smime[128], spath[512], sline[6144];

		/* Drive the loop off fgets so we don't re-process the last
		 * line on EOF / read error. */
		while (fgets(sline, sizeof(sline) - 1, fp) != NULL) {
			terminator = (char *) NULL;
			terminator = strrchr(sline, '\r');
			if (terminator != (char *) NULL)
				*terminator = 0;
			terminator = (char *) NULL;
			terminator = strrchr(sline, '\n');
			if (terminator != (char *) NULL)
				*terminator = 0;

			{
				/* Width specifiers bound each field; sline is
				 * 6144 bytes and a single unbroken word could
				 * otherwise overflow sreq/smime/spath. The widths
				 * are chosen so the reassembled "%s %s %s" fits
				 * in p->data[1024]: 255 + 1 + 127 + 1 + 511 = 895
				 * plus NUL. */
				fields = sscanf(sline, "%255s %127s %511s", sreq, smime, spath);

				/* Test for empty line or comment */
				if ((fields != 3) || (sreq[0] == '#'))
					continue;
				i++;

				/* Allocate space for a new element */
				p = (struct media *) malloc(sizeof(struct media));
				if (p == NULL) {
					fprintf(stderr, "WebJACL: out of memory loading media list\n");
					break;
				}
				oldp->next = p;
				snprintf(p->data, sizeof(p->data),
					 "%s %s %s", sreq, smime, spath);
				p->next = NULL;
				oldp = p;
			}
		}

		fprintf(stderr, MEDIA_REGISTERED,
			i);
	}

	return 0;

}


void
del_tmp_file(void)
{
	extern FILE    *file;

	fclose(file);

}


void
wj_cleanup(int signo)
{
	fprintf(stderr, CLEANING_UP);
	del_tmp_file();
	fflush(stdout);
	fflush(stdin);
	shutdown(clientFd, 2);
	close(clientFd);
	shutdown(serverFd, 2);
	close(serverFd);

	exit(0);
}


void
wj_cleanup_without_exit(void)
{
	fprintf(stderr, CLEANING_UP);
	del_tmp_file();

	fflush(stdout);
	fflush(stdin);
	shutdown(clientFd, 2);
	close(clientFd);
	shutdown(serverFd, 2);
	close(serverFd);
}



/*
 * wj_connect_io( clientFd ) --- Point stdin/stdout at the just-accepted client
 * connection so the existing request-parsing (fgets(stdin)) and response-writing
 * (printf/fwrite to stdout) code works unchanged.
 *
 * POSIX: a connected socket IS a file descriptor, so we dup2() it onto stdout
 * and stdin (saving the originals first). This is exactly the code that lived
 * inline in wj_listen() before the refactor.
 *
 * Windows: a SOCKET is not a file descriptor, so dup2() can't be used. We recv()
 * the HTTP request header block from the socket into a temp file and make that
 * temp file stdin; and we create a second temp file and make it stdout. The
 * response is flushed to the socket later, in wj_disconnect_io().
 */
void
wj_connect_io(wj_socket_t clientFd)
{
#ifdef _WIN32
	FILE           *reqfp, *respfp;
	char            recvbuf[4096];
	int             n, header_end = 0;
	int             reqfd, respfd;

	/*
	 * Read the HTTP request headers from the socket into a temp file. We
	 * stop once we have seen the blank line ("\r\n\r\n") that terminates
	 * the header block. webjacl only ever inspects the request line and a
	 * couple of headers (Cookie), and does not consume a POST body, so the
	 * headers are all we need to buffer.
	 */
	wj_req_tmpname[0] = '\0';
	wj_resp_tmpname[0] = '\0';

	if (tmpnam(wj_req_tmpname) == NULL ||
	    (reqfp = fopen(wj_req_tmpname, "w+b")) == NULL) {
		fprintf(stderr, "WebJACL: cannot create request temp file\n");
		return;
	}

	while (!header_end &&
	       (n = recv(clientFd, recvbuf, (int) sizeof(recvbuf), 0)) > 0) {
		fwrite(recvbuf, 1, (size_t) n, reqfp);
		/*
		 * Cheap end-of-headers detection: a blank line. We re-scan the
		 * whole received chunk each time which is fine for the tiny
		 * request sizes webjacl deals with.
		 */
		if (n >= 4) {
			int i;
			for (i = 0; i <= n - 4; i++) {
				if (recvbuf[i] == '\r' && recvbuf[i + 1] == '\n' &&
				    recvbuf[i + 2] == '\r' && recvbuf[i + 3] == '\n') {
					header_end = 1;
					break;
				}
			}
		}
	}
	fflush(reqfp);
	fclose(reqfp);

	/* Create the response temp file. */
	if (tmpnam(wj_resp_tmpname) == NULL ||
	    (respfp = fopen(wj_resp_tmpname, "w+b")) == NULL) {
		fprintf(stderr, "WebJACL: cannot create response temp file\n");
		remove(wj_req_tmpname);
		wj_req_tmpname[0] = '\0';
		return;
	}

	/* Save the real stdin/stdout, then redirect them at the temp files. */
	wj_savefdOUT = _dup(MYSTDOUT);
	wj_savefdIN = _dup(MYSTDIN);

	respfd = _fileno(respfp);
	_dup2(respfd, MYSTDOUT);
	fclose(respfp);

	if ((reqfp = fopen(wj_req_tmpname, "rb")) != NULL) {
		reqfd = _fileno(reqfp);
		_dup2(reqfd, MYSTDIN);
		fclose(reqfp);
	}
#else
	/*
	 * Redirect file descriptors for stdin/stdout. We do this at file
	 * descriptor level, so that all stdio is automatically redirected too.
	 */
	dup2(MYSTDOUT, wj_savefdOUT);
	dup2(clientFd, MYSTDOUT);

	dup2(MYSTDIN, wj_savefdIN);
	dup2(clientFd, MYSTDIN);
#endif
}


/*
 * wj_disconnect_io( clientFd ) --- Tear a client connection down, restoring the
 * real stdin/stdout and closing the socket. Replaces the
 *   fflush(stdout); dup2(savefdOUT,MYSTDOUT); dup2(savefdIN,MYSTDIN);
 *   shutdown(clientFd,2);
 * idiom that previously appeared inline in every wj_listen() exit path.
 *
 * Windows: the response was buffered into a temp file (stdout). We rewind it,
 * send() its bytes to the socket, restore stdin/stdout, close the socket and
 * delete both temp files.
 */
void
wj_disconnect_io(wj_socket_t clientFd)
{
#ifdef _WIN32
	FILE           *respfp;
	char            sendbuf[4096];
	size_t          n;

	fflush(stdout);

	/* Restore the real stdout/stdin (this also closes our temp-file fds). */
	if (wj_savefdOUT != -1) {
		_dup2(wj_savefdOUT, MYSTDOUT);
		_close(wj_savefdOUT);
		wj_savefdOUT = -1;
	}
	if (wj_savefdIN != -1) {
		_dup2(wj_savefdIN, MYSTDIN);
		_close(wj_savefdIN);
		wj_savefdIN = -1;
	}

	/* Flush the buffered response file out to the socket. */
	if (wj_resp_tmpname[0] != '\0' &&
	    (respfp = fopen(wj_resp_tmpname, "rb")) != NULL) {
		while ((n = fread(sendbuf, 1, sizeof(sendbuf), respfp)) > 0) {
			char *p = sendbuf;
			size_t left = n;
			while (left > 0) {
				int sent = send(clientFd, p, (int) left, 0);
				if (sent <= 0)
					break;
				p += sent;
				left -= (size_t) sent;
			}
		}
		fclose(respfp);
	}

	shutdown(clientFd, 2);
	closesocket(clientFd);

	if (wj_req_tmpname[0] != '\0') {
		remove(wj_req_tmpname);
		wj_req_tmpname[0] = '\0';
	}
	if (wj_resp_tmpname[0] != '\0') {
		remove(wj_resp_tmpname);
		wj_resp_tmpname[0] = '\0';
	}
#else
	fflush(stdout);
	dup2(wj_savefdOUT, MYSTDOUT);
	dup2(wj_savefdIN, MYSTDIN);
	shutdown(clientFd, 2);
#endif
}


/*
 * int wj_listen( void ) This function is called when the application waits
 * for a connection. It blocks until a connection is established on the
 * listening socket, then returns. After a move has been done within the
 * game, the JACL interpreter will save its state and call us again until the
 * next connection arrives (for a next player's move). Since the interpreter
 * does state preservation by itself, we just have to handle the socket and
 * redirect I/O from stdin/stdout to the socket.
 * 
 * FCGI_Accept() is expanded by the preprocessor to call this function, so that
 * code written for fastcgi can use webjacl with minimal modifications. The
 * macro FCGI_Accept is defined in webjacl.h. Since webjacl is designed as a
 * fastcgi _replacement_, it is not possible to compile both into the same
 * executable.
 */

int
wj_listen(void)
{

	/*
	 * We do the server startup and initialization here, because
	 * wj_setup() is too complicated already. server_initialized is
	 * initially 0, meaning that we should setup the server code. Then we
	 * set the variable to 1, and so the next invocation of wj_listen()
	 * does only accept() socket connections.
	 * 
	 * Note: The name of this function might be misleading. It combines
	 * socket(), listen() and accept() into a single function.
	 */
	static int      server_initialized = 0;
	static int      wj_totalconnections = 0;

	char            request[WJ_MAX_REQ_SIZE], request_line[WJ_MAX_REQ_SIZE], webjacl_cookies[WJ_MAX_REQ_SIZE];
	int             port, sockopt;

	int             request_len = 0;

	/* External media database */
	struct media   *mp;
	struct stat     filestat;

	/*
	 * Label where we jump to after executing internal server commands
	 */
listen_again:
	wj_totalconnections++;

	if (!server_initialized) {
#ifdef _WIN32
		/* Winsock must be initialised before any socket call. SIGPIPE
		 * does not exist on Windows (a broken send() just returns an
		 * error), so it is not ignored here. */
		WSADATA wsaData;
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
			fprintf(stderr, "WebJACL: WSAStartup failed\n");
			exit(1);
		}
#else
		signal(SIGPIPE, SIG_IGN);
#endif
		signal(SIGTERM, wj_cleanup);
		signal(SIGINT, wj_cleanup);


		/* Get global port setting into local variable */
		port = wj_port;

		/* Now set up the server socket */
		serverFd = socket(AF_INET, SOCK_STREAM, DEFAULT_PROTOCOL);
		serverLen = sizeof(serverAddress);
		memset((char *) &serverAddress, 0, serverLen);
		serverAddress.sin_family = AF_INET;
		serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
		serverAddress.sin_port = htons(port);
		serverSockAddrPtr = (struct sockaddr *) & serverAddress;

		/* Enable fast socket reuse on development server */
		if (wj_servertype == WJ_DEVSERVER) {
			sockopt = 1;
			if (setsockopt
			    (serverFd, SOL_SOCKET, SO_REUSEADDR, &sockopt,
			     sizeof(sockopt)) == -1) {
				perror("WebJACL:wj_listen()");
				exit(1);
			}
		}
		/* If something went wrong, finish */
		if (bind(serverFd, serverSockAddrPtr, serverLen) != 0) {
			perror("WebJACL:wj_listen()");
			exit(1);
		}
		listen(serverFd, MAXUSER);
		clientFd = -1;
		clientSockAddrPtr = (struct sockaddr *) & clientAddress;
		clientLen = sizeof(clientAddress);

		server_initialized = 1;

	}
	 /* End of server initialization */
	else {
		/*
		 * If a connection already exists, close it, so that the
		 * browser displays our output
		 */
		wj_disconnect_io(clientFd);
		clientFd = -1;
	}

	/* Wait for a connection */

	//fprintf(stderr, "Waiting for a connection.\n");
	if ((clientFd = accept(serverFd, clientSockAddrPtr, &clientLen)) != WJ_INVALID_SOCK) {
		//fprintf(stderr, "Got a connection from %d - %s.\n", clientSockAddrPtr->sa_family, clientSockAddrPtr->sa_data);
		/*
		 * Handle one connection: 1. Connect our stdin/stdout to the
		 * socket. We do this at file descriptor level, so that all
		 * stdio is automatically redirected too. 2. Do some HTTP and
		 * analyze the request (GET, POST) 3. Set HTTP variables in
		 * our environment 4.1. Evaluate internal commands
		 * (diagnostic) OR 4.2. Return to caller for request
		 * processing
		 */

		/* Redirect stdin/stdout to the client connection */
		wj_connect_io(clientFd);


		/*
		 * Read the HTTP request. It may consist of several lines,
		 * and it will end with a line that contains only "\r\n" (2
		 * bytes). We ignore all lines except for the one which
		 * begins with /^GET / or /^POST /
		 */

		request[0] = '\0';

		//fprintf(stderr, "Got a connection, reading first.\n");
		if (fgets(request_line, WJ_MAX_REQ_SIZE - 1, stdin) != NULL) {
			request_len = strlen(request_line);
		} else {
			fprintf(stderr, "WebJACL: NULL request line read.\n");
			request_len = 0;
		}

		fflush(stdin);

		while (request_len > 2) {

			// LOOP THROUGH ALL THE REQUEST
			if (strstr(request_line, "GET ") == request_line) {
				strncpy(request, request_line, WJ_MAX_REQ_SIZE - 1);
			}
			if (strstr(request_line, "POST ") == request_line) {
				strncpy(request, request_line, WJ_MAX_REQ_SIZE - 1);
			}
			if (strstr(request_line, "Cookie") == request_line) {
				request_line[strlen(request_line)-2] = 0;
				strncpy(webjacl_cookies, &request_line[8], WJ_MAX_REQ_SIZE - 1);
			}

			if (fgets(request_line, WJ_MAX_REQ_SIZE - 1, stdin) != NULL) {
				request_len = strlen(request_line);
			} else {
				request_len = 0;
			}

			fflush(stdin);
		}

		if (request[0] == 0) {
			fprintf(stderr, "Invalid request, ignoring.\n");
#ifdef _WIN32
			/* Restore stdin/stdout, flush the (empty) response and
			 * delete the temp files before looping back. */
			wj_disconnect_io(clientFd);
#else
			shutdown(clientFd, 2);
#endif
			clientFd = -1;
			goto listen_again;
		}

		/*
		 * From here on we only use the string variable "request",
		 * which contains exactly the one line with the GET/POST
		 * request
		 */

		/* Look for internal WebJACL commands first */
		if (strstr(request, "GET /server-status") == request) {
			printf
				("HTTP/1.0 200 OK\r\nServer: %s\r\nConnection: close\r\n\r\n",
				 WJ_SERVERNAME);
			printf
				("<HTML><HEAD><TITLE>WebJACL server status</TITLE></HEAD>\n");
			printf
				("<BODY>\n<CENTER><H1>WebJACL server status</H1></CENTER>\n");
			printf("<CENTER><TABLE BORDER=0>\n");
			printf
				("<TR BGCOLOR=\"#a0a0a0\"><TD>Hostname:</TD><TD>%s</TD></TR>\n",
				 wj_hostname);
			printf("<TR BGCOLOR=\"#b0b0b0\"><TD>Port:</TD><TD>%d</TD></TR>\n",
			       wj_port);
			printf
				("<TR BGCOLOR=\"#c0c0c0\"><TD>Server startup time:</TD><TD>%s</TD></TR>\n",
				 ctime(&wj_startuptime));
			printf
				("<TR BGCOLOR=\"#d0d0d0\"><TD>Gamefile served:</TD><TD>%s</TD></TR>\n",
				 wj_script_name);
			printf
				("<TR BGCOLOR=\"#e0e0e0\"><TD>Connections since startup:</TD><TD>%d</TD></TR>\n",
				 wj_totalconnections);
			printf
				("<TR BGCOLOR=\"#f0f0f0\"><TD>Server software:</TD><TD>%s</TD></TR>\n",
				 WJ_SERVERNAME);
			printf("</TABLE></CENTER>\n");
			printf("</BODY></HTML>");
			fflush(stdout);
			goto listen_again;
		}
		/* Parse GET request and extract query string */
		if (strstr(request, "GET ") == request) {
			char           *qstring, portstring[16];

			sprintf(portstring, "%d", wj_port);

			setenv("SERVER_SOFTWARE", WJ_SERVERNAME, 1);
			setenv("SERVER_NAME", wj_hostname, 1);
			setenv("SERVER_PORT", portstring, 1);

			setenv("GATEWAY_INTERFACE", "CGI/1.1", 1);
			setenv("SERVER_PROTOCOL", "HTTP/1.0", 1);
			setenv("REQUEST_METHOD", "GET", 1);
			setenv("SCRIPT_NAME", "", 1);
			setenv("HTTP_COOKIE", webjacl_cookies, 1);
			//setenv("REMOTE_USER", "", 1);

			setenv("CONTENT_TYPE", "", 1);
			setenv("CONTENT_LENGTH", "", 1);

			if ((qstring = strchr(request, '?')) == NULL) {
				/* Copy file name out of request line */
				char            qstring_copy[WJ_MAX_REQ_SIZE],
				               *p, *q;
				for (p = (request + 4), q = qstring_copy;
				     (*p != ' ') && (*p != '\0'); p++, q++)
					*q = *p;
				*q = '\0';

				if (strcmp(qstring_copy, "/") == 0)
					/* This is a "GET /" */
					setenv("QUERY_STRING", "", 1);
				else {
					/* This is an external media request */
					int             requested_item_found = 0;

					/*
					 * Search in the linked list for
					 * qstring_copy
					 */
					//printf("%ld\n", mediadb.next);
					for             (mp = mediadb.next; mp != NULL; mp = mp->next) {
						if (strstr(mp->data, qstring_copy) == mp->data) {
							/* Sized to match wj_setupdb's widths so the
							 * compiler can prove the snprintf below
							 * cannot truncate. fullpath bumped to 8192
							 * to give headroom over wj_script_dir
							 * (4096) + "/" + spath (512). */
							char            sreq[256],
							                smime[128],
							                spath[512], fullpath[8192];
							requested_item_found = 1;

							/*
							 * We've found a
							 * match! Extract the
							 * relevant fields
							 */
							sscanf(mp->data, "%255s %127s %511s",
							    sreq, smime, spath);
							if (wj_script_dir[0] != '\0')
								snprintf(fullpath, sizeof(fullpath),
									 "%s/%s", wj_script_dir, spath);
							else {
								strncpy(fullpath, spath, sizeof(fullpath) - 1);
								fullpath[sizeof(fullpath) - 1] = 0;
							}

							/*
							 * Now read in the
							 * file
							 */
							if (stat(fullpath, &filestat) != 0) {
								/*
								 * Error 404
								 * if stat
								 * fails!
								 */
								printf
									("HTTP/1.0 404 Not Found\r\nServer: %s\r\nConnection: close\r\nContent-Type: text/html\r\n\r\n",
									 WJ_SERVERNAME);
								printf
									("<HTML><HEAD></HEAD><BODY><H1>404 Not Found</H1>Your browser said to WebJACL:<PRE>%s</PRE></BODY></HTML>\r\n",
								   request);
								wj_disconnect_io(clientFd);
								clientFd = -1;
								goto listen_again;
							} else {
								FILE           *media_fp;
								char           *media_buffer;

								/* Cap media reads to keep a multi-GB
								 * file (or fd manipulation that makes
								 * stat return a huge size) from DoSing
								 * the server with a single request. */
								#define WJ_MEDIA_MAX_BYTES (64L * 1024L * 1024L)
								if (filestat.st_size < 0 ||
								    (long) filestat.st_size > WJ_MEDIA_MAX_BYTES) {
									printf
										("HTTP/1.0 413 Payload Too Large\r\nServer: %s\r\nConnection: close\r\nContent-Type: text/html\r\n\r\n",
										 WJ_SERVERNAME);
									printf
										("<HTML><HEAD></HEAD><BODY><H1>413 Payload Too Large</H1></BODY></HTML>\r\n");
									wj_disconnect_io(clientFd);
									clientFd = -1;
									goto listen_again;
								}
								if ((media_buffer =
								     malloc(filestat.st_size + 1)) == NULL) {
									fprintf(stderr,
										"WebJACL:listen(): Panic! Can't malloc!\n");
									exit(0);
								}
								if ((media_fp = fopen(fullpath, "r")) == NULL) {
									/* Don 't panic if we can't read the file. This is a defined 403 condition */
									printf
										("HTTP/1.0 403 Forbidden\r\nServer: %s\r\nConnection: close\r\nContent-Type: text/html\r\n\r\n",
										 WJ_SERVERNAME);
									printf
										("<HTML><HEAD></HEAD><BODY><H1>403 Forbidden</H1>Your browser said to WebJACL:<PRE>%s</PRE></BODY></HTML>\r\n",
										 request);
									wj_disconnect_io(clientFd);
									clientFd = -1;
									goto listen_again;
								}
								/*
								 * Suck it
								 * in!
								 */
								fread(media_buffer, filestat.st_size, 1,
								  media_fp);
								fclose(media_fp);
								printf
									("HTTP/1.0 200 OK\r\nServer: %s\r\nConnection: close\r\nContent-Type: %s\r\nContent-Length: %d\r\n\r\n",
									 WJ_SERVERNAME, smime, (int) filestat.st_size);
								fflush(stdout);
								fwrite(media_buffer, filestat.st_size, 1,
								    stdout);
								fflush(stdout);
								free(media_buffer);
								goto listen_again;
							}
						}
					}

					if (!requested_item_found) {
						/*
						 * We found no match in our
						 * media list
						 */
						printf
							("HTTP/1.0 404 Not Found\r\nServer: %s\r\nConnection: close\r\nContent-Type: text/html\r\n\r\n",
							 WJ_SERVERNAME);
						printf
							("<HTML><HEAD></HEAD><BODY><H1>404 Not Found</H1>Your browser said to WebJACL:<PRE>%s</PRE></BODY></HTML>\r\n",
							 request);
						wj_disconnect_io(clientFd);
						clientFd = -1;
						goto listen_again;
					}
				}
			} else {
				/*
				 * We have a query string here. Copy it to a
				 * temp location and stop at first space.
				 * Also skip leading question mark
				 */
				char            qstring_copy[WJ_MAX_REQ_SIZE],
				               *p, *q;

				for (p = ++qstring, q = qstring_copy;
				     (*p != ' ') && (*p != '\0'); p++, q++)
					*q = *p;
				*q = '\0';
				setenv("QUERY_STRING", qstring_copy, 1);
			}
		}
		/*
		 * Handle POST request by saying that we don't support it for
		 * the moment
		 */
		if (strstr(request, "POST ") == request) {
			printf
				("HTTP/1.0 501 Method Not Implemented\r\nServer: %s\r\nConnection: close\r\nContent-Type: text/html\r\n\r\n",
				 WJ_SERVERNAME);
			printf
				("<HTML><HEAD></HEAD><BODY><H1>Method POST not implemented yet!</H1>Your browser said to WebJACL:<PRE>%s</PRE></BODY></HTML>\r\n",
				 request);
			wj_disconnect_io(clientFd);
			clientFd = -1;
			goto listen_again;
		}

		/* Output some HTTP reply headers */
		printf("HTTP/1.0 200 OK\r\nServer: %s\r\nConnection: close\r\n",
		       WJ_SERVERNAME);
		fflush(stdout);

		return (0);
	} else {
		fprintf(stderr, "How did we end up here?\n");
	}
	return (0);
}
