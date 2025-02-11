all: clean trace

trace:
	../../build/r4r -vv --output out --result a.html --docker-container-name r4r-test-postgress --docker-image-tag r4r/test-postgress R -e 'rmarkdown::render("a.Rmd")'

clean:
	rm -fr a.html out my-db.sqlite
