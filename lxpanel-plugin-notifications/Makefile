# Makefile
#
# Copyright (C) 2014 Kano Computing Ltd.
# License: http://www.gnu.org/licenses/gpl-2.0.txt GNU General Public License v2
#

CC=gcc
CFLAGS= `pkg-config --cflags gtk+-2.0` -g3
LIBS=`pkg-config --libs gtk+-2.0`-lkdesk-hourglass
MODE=755

SRC=kano_notifications.c parson/parson.c config.c ui.c
BIN=kano-notifications-daemon
INSTALL_PATH=/usr/bin

.PHONY: init

build: $(BIN)

#init:
#	cd .. && git submodule init
#	cd .. && git submodule update

install: $(BIN)
	install -p -m $(MODE) $(BIN) $(INSTALL_PATH)

$(BIN): $(SRC)
	$(CC) -Wall $(CFLAGS) $(SRC) -o $(BIN) $(LIBS)

