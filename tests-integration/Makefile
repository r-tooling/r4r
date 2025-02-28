.SUFFIXES:

RUN := docker run --rm -v $$(pwd):/home/r4r/tests -v /var/run/docker.sock:/var/run/docker.sock ghcr.io/r-tooling/r4r-it:latest

SUBDIRS := $(patsubst %/,%,$(sort $(dir $(wildcard */Makefile))))

.PHONY: all $(SUBDIRS) clean

all: $(SUBDIRS)

$(SUBDIRS):
	pwd
	ls -l
	hostname
	$(RUN) hostname
	$(RUN) make -C $@ R4R=r4r
