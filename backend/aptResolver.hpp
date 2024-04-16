#pragma once
#include <string>
#include <unordered_map>
#include <vector>

namespace backend {
	struct AptInfo {
		//a line in sources.list which includes the given repository
		std::u8string source;

	};
	struct AptLookupInfo {
		//a line in sources.list which includes the given repository
		std::u8string installableVersion;
		std::u8string sourceRepoMangled;

	};

	struct Apt {
		Apt();
		AptLookupInfo resolveNameToSourceRepo(const std::u8string& name);
		AptInfo& translatePackageToIdentify(const std::u8string& packageRepoMangledName);

	private:
		std::vector<std::u8string> allRepoInstalls;
		std::unordered_map<std::u8string, AptInfo> repoInstalLookup;
	};
}