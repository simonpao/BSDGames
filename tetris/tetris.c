/*	$NetBSD: tetris.c,v 1.17 2004/01/27 20:30:30 jsm Exp $	*/

/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek and Darren F. Provine.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)tetris.c	8.1 (Berkeley) 5/31/93
 */

#include <sys/cdefs.h>
#ifndef lint
__COPYRIGHT("@(#) Copyright (c) 1992, 1993\n\
	The Regents of the University of California.  All rights reserved.\n");
#endif /* not lint */

/*
 * Tetris (or however it is spelled).
 */

#include <sys/time.h>

#include <err.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "input.h"
#include "scores.h"
#include "screen.h"
#include "tetris.h"

cell	board[B_SIZE];		/* 1 => occupied, 0 => empty */

int	Rows, Cols;		/* current screen size */

const struct shape *curshape;
const struct shape *nextshape;

int piece_selection[7] = { 0, 1, 2, 3, 4, 5, 6 } ;
int piece_sel_ndx = 0 ;

long	fallrate;		/* less than 1 million; smaller => faster */

int	score;			/* the obvious thing */
gid_t	gid, egid;

char	key_msg[150];
int	showpreview;

struct shape * randshape();
static	void	elide(void);
static	void	setup_board(void);
	int	main(int, char **);
	void	onintr(int) __attribute__((__noreturn__));
	void	usage(void) __attribute__((__noreturn__));

struct shape *
randshape()
{
	int i, rnd1, rnd2, tmp;

	if (piece_sel_ndx > 6)
		piece_sel_ndx = 0 ;
	if (piece_sel_ndx == 0)
	{
		for (i = 0; i < 7; i++)
		{
			rnd1 = (int)(random() % 7) ;
			rnd2 = (int)(random() % 7) ;
			tmp = piece_selection[rnd2] ;
			piece_selection[rnd2] = piece_selection[rnd1] ;
			piece_selection[rnd1] = tmp ;
		}
	}
	return (struct shape *) &shapes[piece_selection[piece_sel_ndx++]] ;
}

/*
 * Set up the initial board.  The bottom display row is completely set,
 * along with another (hidden) row underneath that.  Also, the left and
 * right edges are set.
 */
static void
setup_board()
{
	int i;
	cell *p;

	p = board;
	for (i = B_SIZE; i; i--)
		*p++ = i <= (2 * B_COLS) || (i % B_COLS) < 2;
}

/*
 * Elide any full active rows.
 */
static void
elide()
{
	int i, j, base;
	cell *p;

	for (i = A_FIRST; i < A_LAST; i++) {
		base = i * B_COLS + 1;
		p = &board[base];
		for (j = B_COLS - 2; *p++ != 0;) {
			if (--j <= 0) {
				/* this row is to be elided */
				memset(&board[base], 0, B_COLS - 2);
				scr_update();
				tsleep();
				while (--base != 0)
					board[base + B_COLS] = board[base];
				scr_update();
				tsleep();
				break;
			}
		}
	}
}

int
main(argc, argv)
	int argc;
	char *argv[];
{
	int pos, c;
	const char *keys;
	int level = 2;
	char key_write[7][10];
	int ch, i, j;
	int fd;

	gid = getgid();
	egid = getegid();
	setegid(gid);

	fd = open("/dev/null", O_RDONLY);
	if (fd < 3)
		exit(1);
	close(fd);

	keys = "15320pq"; // 1 - left; 5 - up; 3 - right; 2 - down; 0 - drop; p - pause; q - quit

	while ((ch = getopt(argc, argv, "k:l:psh")) != -1)
		switch(ch) {
		case 'k':
			if (strlen(keys = optarg) != 7)
				usage();
			break;
		case 'l':
			level = atoi(optarg);
			if (level < MINLEVEL || level > MAXLEVEL) {
				errx(1, "level must be from %d to %d",
				     MINLEVEL, MAXLEVEL);
			}
			break;
		case 'p':
			showpreview = 1;
			break;
		case 's':
			showscores(0);
			exit(0);
		case '?':
		case 'h':
		default:
			usage();
		}

	argc -= optind;
	argv += optind;

	if (argc)
		usage();

	fallrate = 1000000 / level;

	for (i = 0; i <= 6; i++) {
		for (j = i+1; j <= 6; j++) {
			if (keys[i] == keys[j]) {
				errx(1, "duplicate command keys specified.");
			}
		}
		if (keys[i] == ' ')
			strcpy(key_write[i], "<space>");
		else {
			key_write[i][0] = keys[i];
			key_write[i][1] = '\0';
		}
	}

	sprintf(key_msg,
YEL "%s - left   %s - rotate   %s - right   %s - down   %s - drop   %s - pause   %s - quit" RESET,
		key_write[0], key_write[1], key_write[2], key_write[3], key_write[4],
		key_write[5], key_write[6]);

	(void)signal(SIGINT, onintr);
	scr_init();
	setup_board();

	srandom(getpid());
	scr_set();

	pos = A_FIRST*B_COLS + (B_COLS/2)-1;
	nextshape = randshape();
	curshape = randshape();

	scr_msg(key_msg, 1);

	for (;;) {
		place(curshape, pos, 1);
		scr_update();
		place(curshape, pos, 0);
		c = tgetchar();
		if (c < 0) {
			/*
			 * Timeout.  Move down if possible.
			 */
			if (fits_in(curshape, pos + B_COLS)) {
				pos += B_COLS;
				continue;
			}

			/*
			 * Put up the current shape `permanently',
			 * bump score, and elide any full rows.
			 */
			place(curshape, pos, 1);
			score++;
			elide();

			/*
			 * Choose a new shape.  If it does not fit,
			 * the game is over.
			 */
			curshape = nextshape;
			nextshape = randshape();
			pos = A_FIRST*B_COLS + (B_COLS/2)-1;
			if (!fits_in(curshape, pos))
				break;
			continue;
		}

		/*
		 * Handle command keys.
		 */
		if (c == keys[6]) {
			/* quit */
			break;
		}
		if (c == keys[5]) {
			static char msg[] =
			    BLU "paused - press RETURN to continue" RESET;

			place(curshape, pos, 1);
			do {
				scr_update();
				scr_msg(key_msg, 0);
				scr_msg(msg, 1);
				(void) fflush(stdout);
			} while (rwait((struct timeval *)NULL) == -1);
			scr_msg(msg, 0);
			scr_msg(key_msg, 1);
			place(curshape, pos, 0);
			continue;
		}
		if (c == keys[0]) {
			/* move left */
			if (fits_in(curshape, pos - 1))
				pos--;
			continue;
		}
		if (c == keys[1]) {
			/* turn */
			const struct shape *new = &shapes[curshape->rot];

			if (fits_in(new, pos))
				curshape = new;
			// Let's add a little wiggle room
			else if (fits_in(new, pos + 1)) {
				curshape = new;
				pos++;
			}
			else if (fits_in(new, pos - 1)) {
				curshape = new;
				pos--;
			}
			else if (fits_in(new, pos + B_COLS)) {
				curshape = new;
				pos += B_COLS;
			}
			else if (fits_in(new, pos + 1) && fits_in(new, pos + B_COLS + 1)) {
				curshape = new;
				pos += B_COLS + 1;
			}
			else if (fits_in(new, pos - 1) && fits_in(new, pos + B_COLS - 1)) {
				curshape = new;
				pos += B_COLS - 1;
			}
			continue;
		}
		if (c == keys[2]) {
			/* move right */
			if (fits_in(curshape, pos + 1))
				pos++;
			continue;
		}
		if (c == keys[3]) {
			/* move to bottom */
			while (fits_in(curshape, pos + B_COLS)) {
				pos += B_COLS;
				//score++; // don't increment score for moving down one space
				break ; // only move down once
			}
			continue;
		}
		if (c == keys[4]) {
			/* move to bottom */
			while (fits_in(curshape, pos + B_COLS)) {
				pos += B_COLS;
				score++;
			}
			continue;
		}
		if (c == '\f') {
			scr_clear();
			scr_msg(key_msg, 1);
		}
	}

	scr_clear();
	scr_end();

	(void)printf("Your score:  %d point%s  x  level %d  =  %d\n",
	    score, score == 1 ? "" : "s", level, score * level);
	savescore(level);

	//printf("\nHit RETURN to see high scores, ^C to skip.\n");

	/*while ((i = getchar()) != '\n')
		if (i == EOF)
			break;*/

	showscores(level);

	exit(0);
}

void
onintr(signo)
	int signo __attribute__((__unused__));
{
	scr_clear();
	scr_end();
	exit(0);
}

void
usage()
{
	(void)fprintf(stderr, "usage: tetris-bsd [-ps] [-k keys] [-l level]\n");
	(void)fprintf(stderr, "flags: \n");
	(void)fprintf(stderr, "       [-p] : Preview next piece \n");
	(void)fprintf(stderr, "       [-s] : Show high scores and exit \n");
	(void)fprintf(stderr, "       [-k] : Remap keys (default: 15320pq) \n");
	(void)fprintf(stderr, "       [-l] : Select level (1-9) \n");
	(void)fprintf(stderr, "       [-h] : Show this help text \n");
	exit(1);
}
