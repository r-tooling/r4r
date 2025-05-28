NAME := r-ggplot
IMAGE_TAG := r4r/test-$(NAME)
CONTAINER_NAME := r4r-test-$(NAME)
R4R ?= ../../build/r4r

all: clean trace check

.PHONY: trace
trace:
	$(R4R) \
		-vv \
		--output actual \
		--skip-manifest \
		--docker-image-tag $(IMAGE_TAG) \
		--docker-container-name $(CONTAINER_NAME) \
		R -e 'rmarkdown::render("a.Rmd")'

.PHONY: check
check:
	../snapshot.sh expected actual/result

.PHONY: clean
clean:
	-docker rm $(CONTAINER_NAME)
	-docker rmi -f $(IMAGE_TAG)
	rm -fr actual a.html
