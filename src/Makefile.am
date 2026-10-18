AM_MAKEFLAGS = 
ACLOCAL_AMFLAGS = -I m4

AM_CPPFLAGS = $(DBUS_CFLAGS) $(JSON_C_CFLAGS) $(XRANDR_CFLAGS)
AM_CFLAGS =  -fPIC -Wall $(USB_CFLAGS) -pthread
AM_LDFLAGS = 

if OS_LINUX
AM_CFLAGS +=
AM_LDFLAGS += -lrt
endif

if OS_MACOSX
AM_CFLAGS += -D__MACOS__

endif


bin_PROGRAMS = picolcd picolcdboot
picolcd_DEPENDENCIES = ../lib/libpicolcd.la
picolcd_LDADD = ../lib/libpicolcd.la $(AM_LDFLAGS) -ldl $(DBUS_LIBS) $(JSON_C_LIBS) -pthread
picolcd_SOURCES = main.c picolcd-util.c panel.c panel.h media.c media.h input.c input.h display-monitor.c display-monitor.h ../lib/ortek.c ../include/ortek.h

picolcdboot_DEPENDENCIES = ../lib/libpicolcd.la
picolcdboot_LDADD = ../lib/libpicolcd.la $(AM_LDFLAGS)
picolcdboot_SOURCES = picolcdboot-main.c picolcd-util.c

include_HEADERS = ../include/picolcd.h ../include/driver.h ../include/picolcd-util.h ../include/widgets.h

if HAVE_XRANDR
bin_PROGRAMS += picolcd-display-probe
picolcd_display_probe_SOURCES = display-probe.c display-monitor.h
picolcd_display_probe_LDADD = -lX11 $(XRANDR_LIBS)
endif
