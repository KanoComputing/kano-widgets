# Makefile
#
# Copyright (C) 2014-2019 Kano Computing Ltd.
# License: http://www.gnu.org/licenses/gpl-2.0.txt GNU GPL v2
#

CC=gcc
CFLAGS=`pkg-config --cflags lxpanel gtk+-2.0 libwnck-1.0`
LIBS=`pkg-config --libs lxpanel gtk+-2.0 libwnck-1.0`
SRC=kano_home.c
BIN=kano_home.so
INSTALL_PATH=/usr/lib/arm-linux-gnueabihf/lxpanel/plugins/

build: $(BIN)

install:
	install -p -m 0644 $(BIN) $(INSTALL_PATH)

$(BIN): $(SRC)
	$(CC) -Wall $(CFLAGS) -shared -fPIC $(SRC) -o $(BIN) $(LIBS)
