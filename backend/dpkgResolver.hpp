#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

namespace backend {
	struct DpkgPackage {
		std::u8string packageName;
		std::u8string packageVersion;

		explicit operator std::u8string() {
			return 	packageName + u8"=" + packageVersion;
		}
	};

	struct Dpkg {
		static inline const auto executablePath = std::string("dpkg");


		std::optional<DpkgPackage> resolvePathToPackage(std::filesystem::path);
	private:
		std::unordered_map<std::u8string, DpkgPackage> packageNameToData;

		const DpkgPackage& nameToObject(const std::u8string& name);
	};
}