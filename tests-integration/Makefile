.SUFFIXES:

ROOT_DIR := $(dir $(realpath $(lastword $(MAKEFILE_LIST))))
IMAGE_NAME := ghcr.io/r-tooling/r4r-it:latest

# replace the UID/GID of r4r user which has been created in the r4r-it container
# it is important that it does not collide with any existing users
HOST_UID ?= $(shell id -u)
HOST_GID ?= $(shell id -g)
HOST_USER ?= r4r

RUN := docker \
			 run \
			 --rm \
			 -e HOST_UID=$(HOST_UID) \
			 -e HOST_GID=$(HOST_GID) \
			 -e HOST_USER=$(HOST_USER) \
			 -v $(ROOT_DIR):/home/r4r/tests \
			 -v /var/run/docker.sock:/var/run/docker.sock \
			 $(IMAGE_NAME)

SUBDIRS := $(patsubst %/,%,$(sort $(dir $(wildcard */Makefile))))

.PHONY: all docker-image $(SUBDIRS) clean

all: $(SUBDIRS)

docker-image:
	docker build --rm -t $(IMAGE_NAME) -f Dockerfile ..

ifndef LOCAL

$(SUBDIRS): docker-image
	$(RUN) make -C /home/r4r/tests/$@ R4R=r4r

else

$(SUBDIRS):
	$(MAKE) -C $@ R4R=$(ROOT_DIR)/../build/r4r

endif
