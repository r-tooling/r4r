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
		explicit operator std::string() {
			std::u8string str = operator std::u8string();
			std::string compat{ reinterpret_cast<const char*>(str.data()),str.length() };
			return compat;
		}
	};

	struct Rpkg {
		static inline const auto executablePath = std::string("Rscript");
		std::unordered_map<std::u8string, RpkgPackage> packageNameToData;
		std::unordered_map<std::filesystem::path, std::optional<RpkgPackage>> resolvedPaths;

		std::optional<RpkgPackage> resolvePathToPackage(const std::filesystem::path&);
		bool isKnownRpkg(const std::filesystem::path&);
	};

}
