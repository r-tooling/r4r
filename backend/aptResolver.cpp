#include "aptResolver.hpp"
#include "../processSpawnHelper.hpp"
#include "../stringHelpers.hpp"
#include <vector>
namespace {
	std::vector<std::u8string> listAllRepos() {
		using std::operator""sv;
		std::vector<std::string> str;
		for (auto const& dir_entry : std::filesystem::directory_iterator{ "/etc/apt/sources.list.d/" })
			str.emplace_back(dir_entry.path());

		//grep "^[^#]" / etc / apt / sources.list / etc / apt / sources.list.d/*
		auto dpkgProcess = spawnStdoutReadProcess("grep", ArgvWrapper{ "-h", "^[^#]","/etc/apt/sources.list", str }, []() noexcept {
			setlocale(LC_ALL, "C.UTF-8");
			});
		std::vector<std::u8string> results;
		for (auto buffer : LineIterator{ dpkgProcess.out.get() }) {
			std::u8string_view data{ (const char8_t*)buffer.data(),buffer.size() };
			results.emplace_back(trim(data));
		}
		return results;
	}
}
backend::Apt::Apt() :allRepoInstalls(listAllRepos()), repoInstalLookup{} {

}

backend::AptLookupInfo backend::Apt::resolveNameToSourceRepo(const std::u8string& name) {
	using std::operator""sv;

	//this is a hack because c++ and utf8 sucks
	std::string_view nameSV{ reinterpret_cast<const char*>(name.data()), name.size() };
	ArgvWrapper argv{ "policy", nameSV };

	auto dpkgProcess = spawnStdoutReadProcess("apt-cache", argv, []() noexcept {
		setlocale(LC_ALL, "C.UTF-8");
		});

	enum {
		preamble,
		versionLookup,
		versionTableLookup,
		sectionLookup,
		sourceLookup,
		anotherSourceORVersionLookup,
		done
	} state = preamble;

	std::u8string version;
	std::u8string packageRepo;

	for (auto buffer : LineIterator{ dpkgProcess.out.get() }) {
		std::u8string_view data{ (const char8_t*)buffer.data(),buffer.size() };
		trim(data);
		switch (state)
		{
		case preamble:
			if (data == name + u8":") {
				state = versionLookup;
			}
			break;
		case versionLookup: {
			constexpr auto lookup1 = u8"Installed:"sv;
			if (data.starts_with(lookup1)) {
				data.remove_prefix(lookup1.size());
				version = trim(data);
				state = versionTableLookup;
			}
			break;
		}
		case versionTableLookup: {
			constexpr auto lookup2 = u8"Version table:"sv;
			if (data.find(lookup2) != data.npos) {
				state = sectionLookup;
			}
			break;
		}
		case sectionLookup:
			if (data.find(version) != data.npos) {
				state = sourceLookup;
			}
			break;
		case sourceLookup:
			//hope they are ordered :)
			ltrim(data, u8" \t1234567890"sv);
			packageRepo = trim(data);
			if (data.ends_with(u8"Packages"sv)) {
				data.remove_suffix(u8"Packages"sv.size());
			}
			if (packageRepo != u8"/var/lib/dpkg/status") { //this is a lcoal thing, retry required
				state = done;
			}
			else {
				state = anotherSourceORVersionLookup;
				fprintf(stderr, "Version of %s seems to be unacessible in any package. Attempting recovery.\n", toNormal(name).c_str());
				/*´todo: what do here? I assume fallback to the older version and warn
				libpcre2-8-0:
Installed: 10.40-1+ubuntu20.04.1+deb.sury.org+1
Candidate: 10.40-1+ubuntu20.04.1+deb.sury.org+1
Version table:
*** 10.40-1+ubuntu20.04.1+deb.sury.org+1 100
	   100 /var/lib/dpkg/status
	10.39-3ubuntu0.1 500
	   500 http://archive.ubuntu.com/ubuntu jammy-updates/main amd64 Packages
	   500 http://security.ubuntu.com/ubuntu jammy-security/main amd64 Packages

				   */
			}
			break;
		case anotherSourceORVersionLookup:
			if (data.find(u8"http") != data.npos) {//is a link
				trim(ltrim(data, u8" \t1234567890"sv));
				packageRepo = data;
				state = done;
			}
			else if (data.find(u8"/") == data.npos) {
				ltrim(data, u8"* ");//may begin with ***space
				rtrim(data, u8"1234567890");//will end with priority number
				trim(data);//all that remains is the version;
				fprintf(stderr, "Error finding repo for installed version of package %s = %s \n", toNormal(name).c_str(), toNormal(version).c_str());
				version = data;
				fprintf(stderr, " looking up  fallback version %s \n", toNormal(version).c_str());
				state = sourceLookup;
			}
			else {
				//whatver this is I cannot handle it.
				//TODO: err
				packageRepo = u8"";
				continue;
			}
			break;
		case done:
			break;
		default:
			assert(false);
			break;
		}
		if (state == done) {
			break;
		}
	}
	if (state != done) {
		fprintf(stderr, "Lookup for pacakge %s incomplete - check output \n", toNormal(name).c_str());
	}
	return { version, packageRepo };
}

const backend::AptInfo& backend::Apt::translatePackageToIdentify(const std::u8string& packageRepo)
{

	if (auto it = repoInstalLookup.find(packageRepo); it != repoInstalLookup.end()) {
		return *it;
	}

	std::u8string translatedPackage;
	// https://cloud.r-project.org/bin/linux/ubuntu jammy-cran40/ Packages -> https://cloud.r-project.org/bin/linux/ubuntu jammy-cran40/
	// http://archive.ubuntu.com/ubuntu jammy-updates/main amd64 Packages -> http://archive.ubuntu.com/ubuntu/ jammy-updates main restricted
	std::u8string_view data = packageRepo;
	if (data.ends_with(u8" Packages")) {
		data.remove_suffix(std::u8string_view{ u8" Packages" }.size());
	}
	if (data.ends_with(u8" amd64")) {
		data.remove_suffix(std::u8string_view{ u8" amd64" }.size());
	}
	auto pkgURI = data.substr(0, data.find_first_of(u8" "));
	data.remove_prefix(pkgURI.size() + 1);
	translatedPackage += pkgURI;
	translatedPackage += u8" ";

	std::vector<std::u8string_view> props;
	//if we were with a higher c++ version I'd ipplay range transforms here with splits but alas.
	//(.*)/[^ ](.*) => \1 \2
	while (!data.empty()) {
		auto offset = data.find_first_of(u8"/");
		if (offset == data.npos) {
			translatedPackage += data;
			props.emplace_back(data);
			break;
		}
		else if (offset + 1 < data.size() && data[offset + 1] != u8' ') {//has a character after the /
			//the next character is not a space! Replace the slash with a space
			props.emplace_back(data.substr(0, offset));
			translatedPackage += data.substr(0, offset);
			translatedPackage += u8" ";
			data.remove_prefix(offset + 1);
		}
		else {
			props.emplace_back(data.substr(0, offset + 1));
			translatedPackage += data.substr(0, offset + 1);
			data.remove_prefix(offset + 1);
		}
	}
	for (auto& it : allRepoInstalls) {//try exact match
		if (it.find(translatedPackage) != it.npos) {
			return *repoInstalLookup.emplace(packageRepo, it).first;
		}
	}
	for (auto& it : allRepoInstalls) {//oh well, try piecewise

		if (auto found = it.find(pkgURI); found != it.npos) {
			std::u8string_view rest{ it };
			rest = rest.substr(found + pkgURI.size());
			bool anyFail = false;
			for (decltype(auto) match : props) {
				if (rest.find(match) == rest.npos) {
					anyFail = true;
					break;
				}
			}
			if (!anyFail)
				return *repoInstalLookup.emplace(packageRepo, it).first;
		}
	}

	return *repoInstalLookup.emplace(packageRepo, u8"deb" + translatedPackage).first;
}