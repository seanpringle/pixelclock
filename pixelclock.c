/* pixelclock
 * a different way of looking at time
 *
 * Copyright (c) 2012 Sean Pringle <sean.pringle@gmail.com>
 * Copyright (c) 2005,2008-2009 joshua stein <jcs@jcs.org>
 * Copyright (c) 2005 Federico G. Schwindt
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <err.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/types.h>
#include <regex.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

/* default clock size */
#define DEFSIZE 3

/* default position is along the right side */
#define DEFPOS 'r'

/* so our window manager knows us */
char* win_name = "pixelclock";

/* default hours to highlight (9am, noon, 5pm) */
const float defhours[3] = { 9.0, 12.0, 17.0 };

struct xinfo {
	Display* dpy;
	int dpy_width, dpy_height;
	int screen;
	Window win;
	int size;
	char position;
	GC gc;
	Colormap win_colormap;
	char *tickcolor, *timecolor, *highcolor, *background;
} x;

// cli arg handling
int arg_pos(int argc, char *argv[], char *key)
{
	int i; for (i = 0; i < argc && strcasecmp(argv[i], key); i++);
	return i < argc ? i: -1;
}

char* arg_str(int argc, char *argv[], char *key, char* def)
{
	int i = arg_pos(argc, argv, key);
	return (i > 0 && i < argc-1) ? argv[i+1]: def;
}

int arg_int(int argc, char *argv[], char *key, int def)
{
	int i = arg_pos(argc, argv, key);
	return (i > 0 && i < argc-1) ? strtol(argv[i+1], NULL, 10): def;
}

// once-off regex match. don't use for repeat matching; compile instead
int regquick(char *pat, char *str)
{
	regex_t re; regcomp(&re, pat, REG_EXTENDED|REG_ICASE|REG_NOSUB);
	int r = regexec(&re, str, 0, NULL, 0) == 0 ?1:0;
	regfree(&re); return r;
}

extern char *__progname;

long getcolor(const char *);
void handler(int sig);
void init_x(const char *);
void usage(void);

int
main(int argc, char* argv[])
{
	char *display = NULL;
	int i, y;
	int hourtick, lastpos = -1, newpos = 0;
	struct timeval tv[2];
	time_t now;
	struct tm *t;
	XEvent event;
	int nhihours = 0;
	float *hihours = NULL;

	bzero(&x, sizeof(struct xinfo));

	if (arg_pos(argc, argv, "-h") >= 0 || arg_pos(argc, argv, "-help") >= 0) usage();

	display = arg_str(argc, argv, "-d", arg_str(argc, argv, "-display", NULL));
	x.size  = arg_int(argc, argv, "-s", arg_int(argc, argv, "-size", DEFSIZE));

	x.position = 0;
	if (arg_pos(argc, argv, "-b") >= 0 || arg_pos(argc, argv, "-bottom")) x.position = 'b';
	if (arg_pos(argc, argv, "-t") >= 0 || arg_pos(argc, argv, "-top"   )) x.position = 't';
	if (arg_pos(argc, argv, "-l") >= 0 || arg_pos(argc, argv, "-left"  )) x.position = 'l';
	if (arg_pos(argc, argv, "-r") >= 0 || arg_pos(argc, argv, "-right" )) x.position = 'r';

	x.background = arg_str(argc, argv, "-background", "black");
	x.tickcolor  = arg_str(argc, argv, "-tickcolor",  "royal blue");
	x.timecolor  = arg_str(argc, argv, "-timecolor",  "yellow");
	x.highcolor  = arg_str(argc, argv, "-highcolor",  "green");

	/* get times from args */
	for (i = 0; i < argc; i++)
	{
		if (!regquick("^[0-9]+:[0-9]+$", argv[i])) continue;

		nhihours++;
		hihours = realloc(hihours, nhihours * sizeof(float));

		char *p = argv[i];
		int h = strtol(p, &p, 10);
		int m = strtol(p+1, NULL, 10);

		hihours[nhihours-1] = h + (m / 60.0);
	}
	/* use default times */
	if (!nhihours)
	{
		hihours = (float*)defhours;
		nhihours = sizeof(defhours) / sizeof(float);
	}

	init_x(display);

	signal(SIGINT, handler);
	signal(SIGTERM, handler);

	/* each hour will be this many pixels away */
	hourtick = ((x.position == 'b' || x.position == 't') ? x.dpy_width :
		x.dpy_height) / 24;

	for (;;) {
		if (gettimeofday(&tv[0], NULL))
			errx(1, "gettimeofday");
			/* NOTREACHED */

		now = tv[0].tv_sec;
		if ((t = localtime(&now)) == NULL)
			errx(1, "localtime");
			/* NOTREACHED */

		newpos = (hourtick * t->tm_hour) +
			(float)(((float)t->tm_min / 60.0) * hourtick) - 3;

		/* check if we just got exposed */
		bzero(&event, sizeof(XEvent));
		XCheckWindowEvent(x.dpy, x.win, ExposureMask, &event);

		/* only redraw if our time changed enough to move the box or if
		 * we were just exposed */
		if ((newpos != lastpos) || (event.type == Expose)) {
			XClearWindow(x.dpy, x.win);

			/* draw the current time */
			XSetForeground(x.dpy, x.gc, getcolor(x.timecolor));
			if (x.position == 'b' || x.position == 't')
				XFillRectangle(x.dpy, x.win, x.gc,
					newpos, 0, 6, x.size);
			else
				XFillRectangle(x.dpy, x.win, x.gc,
					0, newpos, x.size, 6);

			/* draw the hour ticks */
			XSetForeground(x.dpy, x.gc, getcolor(x.tickcolor));
			for (y = 1; y <= 23; y++)
				if (x.position == 'b' || x.position == 't')
					XFillRectangle(x.dpy, x.win, x.gc,
						(y * hourtick), 0, 2, x.size);
				else
					XFillRectangle(x.dpy, x.win, x.gc,
						0, (y * hourtick), x.size, 2);

			/* highlight requested times */
			XSetForeground(x.dpy, x.gc, getcolor(x.highcolor));
			for (i = 0; i < nhihours; i++)
				if (x.position == 'b' || x.position == 't')
					XFillRectangle(x.dpy, x.win, x.gc,
						(hihours[i] * hourtick), 0,
						2, x.size);
				else
					XFillRectangle(x.dpy, x.win, x.gc,
						0, (hihours[i] * hourtick),
						x.size, 2);

			lastpos = newpos;

			XFlush(x.dpy);
		}

		sleep(1);
	}

	exit(1);
}

void
init_x(const char *display)
{
	int rc;
	int left = 0, top = 0, width = 0, height = 0;
	XGCValues values;
	XTextProperty win_name_prop;

	if (!(x.dpy = XOpenDisplay(display)))
		errx(1, "unable to open display %s", XDisplayName(display));
		/* NOTREACHED */

	x.screen = DefaultScreen(x.dpy);

	x.dpy_width = DisplayWidth(x.dpy, x.screen);
	x.dpy_height = DisplayHeight(x.dpy, x.screen);

	x.win_colormap = DefaultColormap(x.dpy, DefaultScreen(x.dpy));

	switch (x.position) {
	case 'b':
		left = 0;
		height = x.size;
		top = x.dpy_height - height;
		width = x.dpy_width;
		break;
	case 't':
		left = 0;
		top = 0;
		height = x.size;
		width = x.dpy_width;
		break;
	case 'l':
		left = 0;
		top = 0;
		height = x.dpy_height;
		width = x.size;
		break;
	case 'r':
		width = x.size;
		left = x.dpy_width - width;
		top = 0;
		height = x.dpy_height;
		break;
	}

	x.win = XCreateSimpleWindow(x.dpy, RootWindow(x.dpy, x.screen),
			left, top, width, height,
			0,
			getcolor(x.background),
			getcolor(x.background));

	if (!(rc = XStringListToTextProperty(&win_name, 1, &win_name_prop)))
		errx(1, "XStringListToTextProperty");
		/* NOTREACHED */

	XSetWMName(x.dpy, x.win, &win_name_prop);

	// EWMH support
	Atom dock  = XInternAtom(x.dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
	Atom type  = XInternAtom(x.dpy, "_NET_WM_WINDOW_TYPE", False);
	Atom strut = XInternAtom(x.dpy, "_NET_WM_STRUT", False);

	// become a dock
	XChangeProperty(x.dpy, x.win, type, XA_WINDOW, 32, PropModeReplace, (unsigned char*)&dock, 1);

	unsigned long struts[4] = { 0, 0, 0, 0 };
	if (x.position == 'b') struts[3] = x.size;
	if (x.position == 't') struts[2] = x.size;
	if (x.position == 'r') struts[1] = x.size;
	if (x.position == 'l') struts[0] = x.size;

	// reserve screen space
	XChangeProperty(x.dpy, x.win, strut, XA_CARDINAL, 32, PropModeReplace, (unsigned char*)&struts, 4);

	if (!(x.gc = XCreateGC(x.dpy, x.win, 0, &values)))
		errx(1, "XCreateGC");
		/* NOTREACHED */

	XMapWindow(x.dpy, x.win);

	/* we want to know when we're exposed */
	XSelectInput(x.dpy, x.win, ExposureMask);

	XFlush(x.dpy);
	XSync(x.dpy, False);
}

long
getcolor(const char *color)
{
	int rc;

	XColor tcolor;

	if (!(rc = XAllocNamedColor(x.dpy, x.win_colormap, color, &tcolor, &tcolor)))
		errx(1, "can't allocate %s", color);

	return tcolor.pixel;
}

void
handler(int sig)
{
	XCloseDisplay(x.dpy);

	exit(0);
	/* NOTREACHED */
}

void
usage(void)
{
	fprintf(stderr, "usage: %s %s %s %s\n", __progname,
		"[-display host:dpy] [-left|-right|-top|-bottom] [-size <pixels>]",
		"[-background <color>] [-tickcolor <color>] [-timecolor <color>] [-highcolor <color>]",
		"[time time2 ...]");
	exit(1);
}
