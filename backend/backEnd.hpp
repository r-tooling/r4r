#pragma once
#include "../middleend/middleEnd.hpp"
namespace backend {
	/*
		This endpoint merely attempts to create a directory structure which can be chrooted to and will result in the program being re-runable
	*/
	void chrootBased(const middleend::MiddleEndState&);

	void csvBased(const middleend::MiddleEndState&, absFilePath output);
}