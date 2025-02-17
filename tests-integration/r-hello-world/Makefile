NAME := r-hello-world
IMAGE_TAG := r4r/test-$(NAME)
CONTAINER_NAME := r4r-test-$(NAME)
R4R ?= ../../build/r4r

all: clean trace check

.PHONY: trace
trace:
	$(R4R) \
		-vvv \
		--output actual \
		--skip-manifest \
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
