.SUFFIXES:

ROOT_DIR := $(dir $(realpath $(lastword $(MAKEFILE_LIST))))
IMAGE_NAME := ghcr.io/r-tooling/r4r-it:latest

RUN := docker \
			 run \
			 --rm \
			 -e HOST_UID=$(shell id -u) \
			 -e HOST_GID=$(shell id -g) \
			 -e HOST_USER=ubuntu \
			 -v $(ROOT_DIR):/home/r4r/tests \
			 -v /var/run/docker.sock:/var/run/docker.sock \
			 $(IMAGE_NAME)

SUBDIRS := $(patsubst %/,%,$(sort $(dir $(wildcard */Makefile))))

.PHONY: all docker-image $(SUBDIRS) clean

all: $(SUBDIRS)

docker-image:
	docker build --rm -t $(IMAGE_NAME) -f Dockerfile ..

$(SUBDIRS): docker-image
	$(RUN) make -C /home/r4r/tests/$@ R4R=r4r
