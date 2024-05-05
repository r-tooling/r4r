#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>
#include "aptResolver.hpp"

namespace backend {
	struct DpkgPackage {
		std::u8string packageName;
		std::u8string packageVersion;
		const AptInfo& packageRepo;

		explicit operator std::u8string() const noexcept {
			return 	packageName + u8"=" + packageVersion;
		}
		explicit operator std::string() const noexcept {
			std::u8string str = operator std::u8string();
			std::string compat{ reinterpret_cast<const char*>(str.data()),str.length() };
			return compat;
		}
	};

	struct Dpkg {
		static inline const auto executablePath = std::string("dpkg");


		std::optional<const DpkgPackage *> resolvePathToPackage(std::filesystem::path);
		std::unordered_set<DpkgPackage, Hasher<&DpkgPackage::packageName>, Compare<&DpkgPackage::packageName>> packageNameToData;
		
		Apt aptResolver;

	private:
		const DpkgPackage& nameToObject(const std::u8string& name);
	};
}