IMAGE_TAG := r4r/test-$(NAME)
CONTAINER_NAME := r4r-test-$(NAME)
R4R ?= ../../build/r4r
NCPUS ?= 8

define install_r_packages
	Rscript -e 'install.packages(setdiff(commandArgs(TRUE), rownames(installed.packages())), repos="https://cloud.r-project.org", Ncpus=$(NCPUS))' $(1)
endef

all: clean trace check

.PHONY: test-run
test-run:
	$(COMMAND)

.PHONY: trace
trace:
	$(R4R) \
		-vv \
		--output actual \
		--skip-manifest \
		--docker-image-tag $(IMAGE_TAG) \
		--docker-container-name $(CONTAINER_NAME) \
		$(COMMAND)

.PHONY: check
check:
	pwd
	../snapshot.sh --fail expected actual/result

.PHONY: clean
clean:
	-docker rm $(CONTAINER_NAME)
	-docker rmi -f $(IMAGE_TAG)
	rm -fr actual $(ARTIFACTS)
