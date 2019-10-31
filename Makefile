# Copyright 2012 Thomas Gläßle < t [underscore] glaessle [at] gmx [dot] de >
#
# Heavily inspired and copied from :
# pidgin-latex Makefile
# Copyright 2004-2009 Edouard Geuten <thegrima AT altern DOT org>
# Gaim Extended Preferences Plugin Main Makefile
# Copyright 2004-2009 Kevin Stange <extprefs@simguy.net>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#

CC := gcc
LIBTOOL := libtool

ifeq ($(PREFIX),)
  LIB_INSTALL_DIR = $(HOME)/.purple/plugins
else
  LIB_INSTALL_DIR = $(PREFIX)/lib/pidgin
endif

NAME = pidgin-tts

CFLAGS = $(shell pkg-config --cflags pidgin gtk+-2.0)
LDLIBS = $(shell pkg-config --libs pidgin gtk+-2.0)

all: $(NAME).so

install: all
	mkdir -p $(LIB_INSTALL_DIR)
	cp $(NAME).so $(LIB_INSTALL_DIR)

$(NAME).so: $(NAME).o
	$(CC) $(LDFLAGS) -shared $< -o $@ $(LDLIBS) -Wl,--export-dynamic -Wl,-soname

$(NAME).o:$(NAME).c
	$(CC) $(CFLAGS) -fPIC -Wall -c $< -o $@ -DHAVE_CONFIG_H

clean:
	rm -rf *.o *.c~ *.h~ *.so *.la .libs
