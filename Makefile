SRCDIR = ./src

.PHONY: all build run debug clean help

all:
	$(MAKE) -C $(SRCDIR) all

build:
	$(MAKE) -C $(SRCDIR) build

run:
	$(MAKE) -C $(SRCDIR) run

debug:
	$(MAKE) -C $(SRCDIR) debug

clean:
	$(MAKE) -C $(SRCDIR) clean

help:
	$(MAKE) -C $(SRCDIR) help