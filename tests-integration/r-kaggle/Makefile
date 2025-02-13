all: clean trace

trace:
	../../build/r4r \
		-v \
		--output out \
		--result code/titanic-2nd-degree-families-and-majority-voting.html \
		--docker-container-name r4r-test-kaggle \
		--docker-image-tag r4r/test-kaggle \
		R -e 'rmarkdown::render("code/titanic-2nd-degree-families-and-majority-voting.Rmd")'

clean:
	rm -fr a.html out
