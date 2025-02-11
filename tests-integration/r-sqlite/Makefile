all: clean trace

trace:
	../../build/r4r -vvv --output out --result a.html --result my-db.sqlite --docker-container-name r4r-test-sqlite --docker-image-tag r4r/test-sqlite R -e 'rmarkdown::render("a.Rmd")'

clean:
	rm -fr a.html out my-db.sqlite
