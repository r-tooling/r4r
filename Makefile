BUILD_DIR ?= build

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

