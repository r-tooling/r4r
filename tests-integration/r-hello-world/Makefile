NAME := r-hello-world
IMAGE_TAG ?= r4r/test-$(NAME)
CONTAINER_NAME ?= r4r-test-$(NAME)
R4R ?= ../../build/r4r
BASE_IMAGE ?= ubuntu:22.04

all: clean trace check

.PHONY: trace
trace:
	$(R4R) \
		-vv \
		--output actual \
		--skip-manifest \
		--default-image-file ../$(BASE_IMAGE).cache \
		--docker-image-tag $(IMAGE_TAG) \
		--docker-container-name $(CONTAINER_NAME) \
		R -f hello-world.R

.PHONY: check
check:
	../snapshot.sh --fail expected actual/result

.PHONY: clean
clean:
	-docker rm $(CONTAINER_NAME)
	-docker rmi -f $(IMAGE_TAG)
	rm -fr actual hello-world.txt
