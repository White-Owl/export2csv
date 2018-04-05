# Copyright 2016-2018, George Brink

PROJECT = export2csv
SOURCES = export2csv.c

ifeq ($(OS),Windows_NT)
	TARGETDIR = D:\utils
	LIBS = -lodbc32
	INCLUDE =
	PROJECT := $(PROJECT).exe
else
	TARGETDIR = /app/bin
	LIBS = -lodbc

	# for Red Hat you need to uncomment these:
	#LIBS = $(shell odbc_config --libs)
	#INCLUDE = $(shell odbc_config --cflags)
endif
OBJS = $(patsubst %.c, %.o, $(SOURCES))

all: $(PROJECT)

install: $(PROJECT)
	cp $(PROJECT) $(TARGETDIR)

clean:
	rm $(OBJS) $(PROJECT)

$(PROJECT): $(OBJS)
	gcc -o$@  $(OBJS) $(LIBS)

$(OBJS): makefile

%.o: %.c
	gcc -c -Wall -Werror -std=c99 -I$(INCLUDE) -o$@ $<
