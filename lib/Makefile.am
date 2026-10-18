# Changes by bmcleodcd: 2026-09-28.
# Modification/licence notice added by bmcleodcd: 2026-10-18.
# This file is part of picoLCD and is distributed under the GNU General
# Public License, version 2. See COPYING for the terms and warranty disclaimer.
AM_MAKEFLAGS = 
ACLOCAL_AMFLAGS = -I m4

AM_CPPFLAGS =
AM_CFLAGS =  -Wall -fPIC  $(USB_CFLAGS)
AM_LDFLAGS = 

if OS_LINUX
AM_CFLAGS +=
AM_LDFLAGS += -lrt
endif
if OS_MACOSX
AM_CFLAGS += -D__MACOS__
AM_LDFLAGS += 
endif





lib_LTLIBRARIES = libpicolcd.la
libpicolcd_la_DEPENDENCIES = 
libpicolcd_la_LIBADD = $(USB_LIBS) $(AM_LDFLAGS)
libpicolcd_la_SOURCES = \
	hid.c \
	util.c \
	picolcd.c \
	picolcd-common.c \
	splash.c \
	picolcd-20x2.c \
	picolcd-20x4.c \
	picolcd-256x64.c \
	widgets.c \
	rc5.c

