# pixelclock

## Usage

	pixelclock [-display host:dpy] [-left|-right|-top|-bottom] [-size <pixels>]
		[-background <color>] [-tickcolor <color>] [-timecolor <color>] [-highcolor <color>]
		[time time2 ...]

* Colors are X11 named colors.
* Times are of format **hh:mm**.
* Long-form command line arguments are required.

## Original Idea

https://jcs.org/notaweblog/2005/06/28/pixelclock/

## Changes in this Fork

* Convert to EWMH panel with struts.
* Add configurable colors.
* Add Xinerama/Xrandr check to:
	* Avoid crossing monitor edges.
	* Support monitors with different sizes (root window dead space problem).