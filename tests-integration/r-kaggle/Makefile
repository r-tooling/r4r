NAME := r-kaggle
COMMAND := R -e 'rmarkdown::render("code/titanic-2nd-degree-families-and-majority-voting.Rmd")'
ARTIFACTS := code/Titanic_select.csv code/titanic-2nd-degree-families-and-majority-voting.html

include ../test.mk

setup:
	$(call install_r_packages,Hmisc knitr ggplot2 dplyr caret randomForest gridExtra ROCR corrplot kernlab gbm)
