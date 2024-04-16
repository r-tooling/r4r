#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include "aptResolver.hpp"

namespace backend {
	struct DpkgPackage {
		std::u8string packageName;
		std::u8string packageVersion;
		std::u8string packageRepo;

		explicit operator std::u8string() {
			return 	packageName + u8"=" + packageVersion;
		}
		explicit operator std::string() {
			std::u8string str = operator std::u8string();
			std::string compat{ reinterpret_cast<const char*>(str.data()),str.length() };
			return compat;
		}
	};

	struct Dpkg {
		static inline const auto executablePath = std::string("dpkg");


		std::optional<DpkgPackage> resolvePathToPackage(std::filesystem::path);
		std::unordered_map<std::u8string, DpkgPackage> packageNameToData;

	private:
		Apt aptResolver;
		const DpkgPackage& nameToObject(const std::u8string& name);
	};
}