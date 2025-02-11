all: clean trace

trace:
	../../build/r4r -v --output out --result a.html --docker-container-name r4r-test-ggplot --docker-image-tag r4r/test-ggplot R -e 'rmarkdown::render("a.Rmd")'

clean:
	rm -fr a.html out
