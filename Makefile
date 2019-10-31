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
