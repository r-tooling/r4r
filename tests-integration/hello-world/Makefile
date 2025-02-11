all: clean trace

trace:
	../../build/r4r -vvv --output out --result hello-world.txt --docker-image-tag r4r/test-hw ./script.sh

clean:
	- docker rmi -f r4r/test-hw
	- docker rm test
	rm -fr out hello-world.txt
