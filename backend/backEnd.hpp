#pragma once
#include "../middleend/middleEnd.hpp"
namespace backend {
	/*
		This endpoint merely attempts to create a directory structure which can be chrooted to and will result in the program being re-runable

		Is not currently up to date.
	*/
	void chrootBased(const middleend::MiddleEndState&);
	/*
		This endpoint generates a csv list of accessed files and a script for creating a docker container
	*/
	void csvBased(const middleend::MiddleEndState&, absFilePath output);
}