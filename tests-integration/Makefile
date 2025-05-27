.SUFFIXES:

ROOT_DIR := $(dir $(realpath $(lastword $(MAKEFILE_LIST))))

RUN := docker \
			 run \
			 --rm \
			 -e R4R_UID=$(shell id -u) \
			 -e R4R_GID=$(shell id -g) \
			 -v $(ROOT_DIR):/home/r4r/tests \
			 -v /var/run/docker.sock:/var/run/docker.sock \
			 ghcr.io/r-tooling/r4r-it:latest

SUBDIRS := $(patsubst %/,%,$(sort $(dir $(wildcard */Makefile))))

.PHONY: all $(SUBDIRS) clean

all: $(SUBDIRS)

$(SUBDIRS):
	$(RUN) make -C /home/r4r/tests/$@ R4R=r4r
