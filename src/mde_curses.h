/**
 * @file mde_curses.h
 * @brief Portable wide-character ncurses include wrapper across Linux and UNIX.
 */

#ifndef MDE_CURSES_H
#define MDE_CURSES_H

#ifndef _XOPEN_SOURCE_EXTENDED
#define _XOPEN_SOURCE_EXTENDED 1
#endif

#ifdef MDE_USE_CURSES_H
#include <curses.h>
#else
#include <ncursesw/curses.h>
#endif

#endif // MDE_CURSES_H
