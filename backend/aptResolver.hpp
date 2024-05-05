#pragma once
#include <string>
#include <unordered_set>
#include <vector>
#include "../memberTypeHashCompare.hpp"

namespace backend {
	struct AptInfo {
		std::u8string mangledPackageRepoName;
		//a line in sources.list which includes the given repository
		std::u8string source;

	};
	struct AptLookupInfo {
		//a line in sources.list which includes the given repository
		std::u8string installableVersion;
		std::u8string sourceRepoMangled;

	};

	struct Apt {
		using AptInfoMap = std::unordered_set<AptInfo,
			Hasher<&AptInfo::mangledPackageRepoName>,
			Compare<&AptInfo::mangledPackageRepoName>>;


		Apt();
		AptLookupInfo resolveNameToSourceRepo(const std::u8string& packageName);
		//items are never invalidated. this is depended upon elsewhere in the project, retain.
		const AptInfo& translatePackageToIdentify(const std::u8string& packageRepoMangledName);

		const AptInfoMap& encounteredRepositories() {
			return repoInstalLookup;
		}

	private:
		std::vector<std::u8string> allRepoInstalls;
		AptInfoMap repoInstalLookup;

	};
}