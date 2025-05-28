NAME := r-hello-world
COMMAND := R -f hello-world.R
ARTIFACTS := hello-world.txt

include ../test.mk

setup:
	$(call install_r_packages,stringr)

