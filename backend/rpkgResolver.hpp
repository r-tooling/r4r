#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include "../memberTypeHashCompare.hpp"

namespace backend {
	struct RpkgPackage {
		
		std::u8string packageName;
		std::u8string packageVersion;
		std::u8string rVersion;
		std::unordered_set<std::u8string> dependsOn;
		std::filesystem::path whereLocated;

		bool isBaseRpackage;

		RpkgPackage(std::u8string packageName, std::u8string packageVersion, std::u8string rVersion, std::unordered_set<std::u8string> dependsOn,
			std::filesystem::path whereLocated,
			bool isBaseRpackage)
			:packageName{ packageName }, packageVersion{ packageVersion }, rVersion{ rVersion }, dependsOn{ dependsOn }, 
			whereLocated{ whereLocated }, isBaseRpackage{ isBaseRpackage }
		{
		}

		explicit operator std::u8string() const noexcept {
			std::u8string result = packageName + u8"-" + packageVersion + u8": " + rVersion;
			return result;
		}
		explicit operator std::string() const noexcept {
			std::u8string str = operator std::u8string();
			std::string compat{ reinterpret_cast<const char*>(str.data()),str.length() };
			return compat;
		}
	};

	using RpkgSet = std::unordered_set<RpkgPackage, Hasher<&RpkgPackage::packageName>,
		Compare<&RpkgPackage::packageName>>;
	

	struct TopSortIterator {

		const RpkgSet& items;

		auto begin() {
			return iterator{ items };
		}
		auto end() {
			return EndSentinel{};
		}

		struct ExpendedRpkg {
			const RpkgPackage* package;
			mutable std::unordered_set<const RpkgPackage*> dependsOnMe{};
			mutable std::unordered_set<const RpkgPackage*> thisDependsOn{};
		};

		struct EndSentinel {};
		struct iterator {
			std::unordered_set<ExpendedRpkg,Hasher<&ExpendedRpkg::package>, Compare<&ExpendedRpkg::package>>
				toIterateOver;//owns data identified by package*
			std::unordered_set<const ExpendedRpkg*> dependsOnNone;//points to owned data.

			/* or using iterators if need be.
			template<class Begin, class End> requires requires(Begin b, End e){
				{ b != e } -> std::same_as<bool>;
				{ ++b} -> std::same_as<Begin>;
				{ *b} -> std::convertible_to<const RpkgPackage&>;
			}*/
			iterator(const RpkgSet & items) noexcept {
				for (const auto& package : items) {
					toIterateOver.emplace(&package);
				}
				for (const auto& package : items) {
					for (const auto& dependingOn : package.dependsOn) {
						auto* depOnPtr = &*items.find(dependingOn);
						//guaranteed not to point outside the set.
						toIterateOver.find(&package)->thisDependsOn.emplace(depOnPtr);
						toIterateOver.find(depOnPtr)->dependsOnMe.emplace(&package);
					}
				}
				for (const auto& package : toIterateOver) {
					if (package.thisDependsOn.empty()) {
						dependsOnNone.emplace(&package);
					}
				}
				if (dependsOnNone.empty() && !toIterateOver.empty()) {
					fprintf(stderr, "Cycle detected during topsorting of R packages. Tagging a random package as having no dependencies. May cause more warning later on \n");
					dependsOnNone.emplace(&*toIterateOver.begin());
				}
			}

			const iterator& operator++() noexcept {
				auto* removingItem = *dependsOnNone.begin();//if this fails the iterator was invalid
				for (auto* depOnMe : removingItem->dependsOnMe) {
					if (auto pkg = toIterateOver.find(depOnMe); pkg != toIterateOver.end() && pkg->thisDependsOn.erase(removingItem->package) == 1) {
						if (pkg->thisDependsOn.empty()) {
							dependsOnNone.emplace(&*pkg);
						}
					}
				}
				dependsOnNone.erase(removingItem);//I do not own the pointer.
				//toIterateOver.erase(removingItem->package); c++23 :/
				toIterateOver.erase(*removingItem);//I own the pointer

				if (dependsOnNone.empty() && !toIterateOver.empty()) {
					fprintf(stderr, "Cycle detected during topsorting of R packages. Tagging a random package as having no dependencies. May cause more warning later on \n");
					dependsOnNone.emplace(&*toIterateOver.begin());
				}
				return *this;
			}
			const RpkgPackage& operator*() noexcept {
				auto it = dependsOnNone.begin(); 
				return *(*it)->package; //for a valid iterator this has to be valid.
			}

			bool operator !=(const EndSentinel&)const noexcept {
				return !toIterateOver.empty();
			}
		};
	};

	struct Rpkg {
	public:

		bool exectuablePresent;
		Rpkg();

		static inline const auto executablePath = std::string("Rscript");
		RpkgSet packageNameToData;
		//This needs to be a member variable due to lifetime. 
		//As a temporary it would have to be a part of TopSortIterator as created tempoeraries get their lifetime extended in c++23 on only.
		//But TopSortIterator should not have access to the internals of rpkg-based lookups.
		std::unordered_map<std::filesystem::path, std::optional<const RpkgPackage*>> resolvedPaths;

		std::optional<const RpkgPackage*> resolvePathToPackage(const std::filesystem::path&);
		
		bool isKnownRpkg(const std::filesystem::path&);

		std::unordered_set < std::filesystem::path > getLibraryPaths();

		//this is assumed to be only called after we have resolved all other files.
		TopSortIterator topSortedPackages() noexcept {
			//emplace may cause rehas and this iterator invalidation, so the entire thing is done in batches.
			{
				RpkgSet AllPackages = packageNameToData;
				RpkgSet toAdd;
				bool toAddEmpty = false;
				do{
					for (const auto& package : AllPackages) {
						for (const auto& deps : package.dependsOn) {
							if (!AllPackages.contains(deps) && !toAdd.contains(deps)) {
								toAdd.emplace(resolvePackageToName_noinsert(deps));
							}
						}
					}
					toAddEmpty = toAdd.empty();
					AllPackages.merge(std::move(toAdd));
				} while (!toAddEmpty);
				packageNameToData = AllPackages;
			}//end lifetime guard

			return TopSortIterator{ packageNameToData };
		}
	private:
		RpkgPackage resolvePackageToName_noinsert(const std::u8string& packageName);
	};

}
