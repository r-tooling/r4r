#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

namespace backend {
	struct RpkgPackage {
		std::u8string packageName;
		std::u8string packageVersion;
		std::u8string rVersion;
		explicit operator std::u8string() {
			std::u8string result = packageName + u8"-" + packageVersion + u8": " + rVersion;
			return result;
		}
	};

	struct Rpkg {
		static inline const auto executablePath = std::string("Rscript");
		std::unordered_map<std::filesystem::path, std::optional<RpkgPackage>> resolvedPaths;

		std::optional<RpkgPackage> resolvePathToPackage(const std::filesystem::path&);
	};

}