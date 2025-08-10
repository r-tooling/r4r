.DEFAULT_GOAL := all
.PHONY: all configure build test install release format lint clean help

# configuration
BUILD_DIR        ?= build
GENERATOR        ?= Ninja
BUILD_TYPE       ?= Debug
INSTALL_PREFIX   ?= /usr/local
CLANG_FORMAT     ?= clang-format
CLANG_TIDY       ?= clang-tidy
CMAKE            ?= cmake
CTEST            ?= ctest

# docker settings
IMAGE_NAME ?= ghcr.io/r-tooling/r4r:latest

# cmake configuration arguments
CMAKE_ARGS += -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
CMAKE_ARGS += -GNinja

# project structure
SOURCE_DIR       = src
TEST_DIR         = tests

# file patterns
FORMAT_PATTERNS = *.cpp *.hpp *.c *.h
FORMAT_EXCLUDE  = 

COVERAGE_BUILD_DIR    = $(BUILD_DIR)-coverage
COVERAGE_REPORT       = $(COVERAGE_BUILD_DIR)/coverage.info
COVERAGE_REPORT_HTML  = $(COVERAGE_BUILD_DIR)/coverage

#-------------------------------------------------------------------------------
# Targets
#-------------------------------------------------------------------------------

all: build test ## Build and test the project (default)

configure: ## Configure CMake project
	mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && $(CMAKE) $(CMAKE_ARGS) -DCMAKE_INSTALL_PREFIX=$(INSTALL_PREFIX) ..

build: configure ## Build the project
	$(CMAKE) --build $(BUILD_DIR)

test: build ## Run tests
	cd $(BUILD_DIR) && $(CTEST) --output-on-failure

coverage: ## Run tests with code coverage
	$(MAKE) build BUILD_DIR=$(COVERAGE_BUILD_DIR) CMAKE_ARGS='$(CMAKE_ARGS) -DENABLE_COVERAGE=ON'
	cd $(COVERAGE_BUILD_DIR) && \
		$(CTEST) --output-on-failure -T Test -T Coverage
	lcov --capture --directory $(COVERAGE_BUILD_DIR) --output-file $(COVERAGE_REPORT)
	lcov --remove $(COVERAGE_REPORT) '/usr/*' --output-file $(COVERAGE_REPORT)
	lcov --remove $(COVERAGE_REPORT) '*/tests/*' --output-file $(COVERAGE_REPORT)
	lcov --remove $(COVERAGE_REPORT) '*/_deps/*' --output-file $(COVERAGE_REPORT)
	lcov --list $(COVERAGE_REPORT)

install: build ## Install the project
	$(CMAKE) --install $(BUILD_DIR) --prefix $(INSTALL_PREFIX)

release: test ## Create a release archive
	$(MAKE) test BUILD_TYPE=Release
	@RELEASE_NAME=$${RELEASE_NAME:-r4r-$$(git rev-parse --short HEAD).tar.gz}; \
	tar -czf "$$RELEASE_NAME" -C $(BUILD_DIR) r4r -C .. README.md LICENSE; \
	echo "Release archive created: $$RELEASE_NAME"

format: ## Format source code
	@find $(SOURCE_DIR) $(TEST_DIR) \
		-type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.c" -o -name "*.h" \) \
		-not -path "$(FORMAT_EXCLUDE)" \
		-exec $(CLANG_FORMAT) -i {} +

lint: build ## Run static analysis
	@find $(SOURCE_DIR) $(TEST_DIR) \
		-type f \( -name "*.cpp" -o -name "*.hpp" \) \
		-not -path "$(FORMAT_EXCLUDE)" \
		-exec $(CLANG_TIDY) -p $(BUILD_DIR) --warnings-as-errors='*' {} +

clean: ## Clean build artifacts
	@rm -rf $(BUILD_DIR)
	@rm -rf $(COVERAGE_BUILD_DIR)

docker-image:
	docker build --rm -t $(IMAGE_NAME) -f .devcontainer/Dockerfile .

integration-tests:
	$(MAKE) -C tests-integration

help: ## Show this help message
	@awk 'BEGIN {FS = ":.*?## "} /^[a-zA-Z_-]+:.*?## / {printf "\033[36m%-20s\033[0m %s\n", $$1, $$2}' $(MAKEFILE_LIST)

# enable build ccache by default (if available)
ifneq ($(shell command -v ccache),)
  CMAKE_ARGS += -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
endif

# TODO: Enable address sanitizer in debug builds
