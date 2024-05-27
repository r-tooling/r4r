#pragma once
#include "../middleend/middleEnd.hpp"
#include "./rpkgResolver.hpp"
#include "./dpkgResolver.hpp"
#include <unordered_map>

namespace backend {

	class CachingResolver {
		const middleend::MiddleEndState& state;

		Rpkg rpkgResolver {};
		Dpkg dpkgResolver{};

		std::vector<middleend::MiddleEndState::file_info*> getUnmatchedFiles();
		std::vector<middleend::MiddleEndState::file_info*> getExecutedFiles();
	public:
		CachingResolver(const middleend::MiddleEndState& state) :state(state) {}
		void resolveRPackages();
		void resolveDebianPackages();
		/*
			This endpoint generates a csv list of accessed files and a script for creating a docker container
		*/
		void csv(absFilePath output);
		void report(absFilePath output);
		void dockerImage(absFilePath output);
	};

	/*
		This endpoint merely attempts to create a directory structure which can be chrooted to and will result in the program being re-runable

		Is not currently up to date.
	*/
	void chrootBased(const middleend::MiddleEndState&);

}