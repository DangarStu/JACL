/* version.h --- Version constants for the JACL interpreters.
 * (C) 2008 Stuart Allen, distribute and use 
 * according to GNU GPL, see file COPYING for details.
 */

#define J_VERSION            4
#define J_RELEASE            7
#define J_BUILD              1

/* The game-file format version, stamped into release .j2 files as
 * #processed:<n> by jpp and checked on load: an interpreter refuses any game
 * stamped with a higher number. Raise it only when the format changes, not
 * with every release, or the download packages the website rebuilds on each
 * deploy stop loading in the apps people already have installed. */
#define INTERPRETER_VERSION  470
