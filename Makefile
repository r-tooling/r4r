BUILD_DIR ?= build
DEVCONTAINER_NAME ?= r-tooling/r4r-dev
DOCKER_IMAGE_NAME ?= r-tooling/r4r

.PHONY: all
all: build

.PHONY: build
build: setup compile

.PHONY: setup
setup:
	mkdir -p $(BUILD_DIR)
	cmake -DCMAKE_BUILD_TYPE=Debug -B $(BUILD_DIR) -G Ninja .

.PHONY: compile
compile: setup
	ninja -C $(BUILD_DIR)

.PHONY: clean
clean:
	rm -fr $(BUILD_DIR)

.PHONY: test
test: compile
	cd $(BUILD_DIR) && ctest --output-on-failure

.PHONY: check
check:
	cppcheck --enable=all --suppress=missingIncludeSystem r4r

.PHONY: devcontainer
devcontainer:
	devcontainer build --workspace-folder . --image-name $(DEVCONTAINER_NAME)

.PHONY: docker-image
docker-image: devcontainer
	docker build --rm -t $(DOCKER_IMAGE_NAME) .

.PHONY: install
install: clean
	mkdir -p $(BUILD_DIR)
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug .
	cmake --build $(BUILD_DIR) --target r4r -j
	cmake --install $(BUILD_DIR)
