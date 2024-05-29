#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <ostream>
#include "aptResolver.hpp"

namespace backend {
	struct DpkgPackage {
		std::u8string packageName;
		std::u8string packageVersion;
		std::u8string aptVersion;
		
		const AptInfo& packageRepo;

		explicit operator std::u8string() const noexcept {
			return 	packageName + u8"=" + aptVersion;
		}
		explicit operator std::string() const noexcept {
			std::u8string str = operator std::u8string();
			std::string compat{ reinterpret_cast<const char*>(str.data()),str.length() };
			return compat;
		}
	};

	struct Dpkg {
		static inline const auto executablePath = std::string("dpkg");


		std::unordered_map<std::filesystem::path, std::optional<const DpkgPackage*>> resolvedPaths;

		
		std::unordered_map<std::filesystem::path, std::optional<const DpkgPackage *>> batchResolvePathToPackage(std::unordered_set<std::filesystem::path> packages, bool trivialOnly = false);
		std::unordered_set<DpkgPackage, Hasher<&DpkgPackage::packageName>, Compare<&DpkgPackage::packageName>> packageNameToData;
		

		Apt aptResolver;

		bool areDependenciesPresent();

		void persist(std::ostream& output);

	private:
		const DpkgPackage& nameToObject(const std::u8string& name);
		std::optional<const backend::DpkgPackage*> resolvePathToPackage(const std::filesystem::path& path);

		std::unordered_map<std::u8string, const DpkgPackage*> alternateLookup;

	};
}