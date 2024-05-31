#pragma once 
#include <string>
#include <string_view>
#include <algorithm>
#include <vector>


template<class T, class Trim = std::u8string_view>
constexpr T&& trim(T&& str, Trim&& what =  u8" \n")
{
	//trim but no ranges to be had.
	str.remove_prefix(std::min(str.find_first_not_of(what), str.size()));
	if (auto pos = str.find_last_not_of(what); pos != str.npos) {
		str.remove_suffix(str.size() - pos - 1);
	}
	else {
		str.remove_suffix(str.size());
	}
	return str;
}
template<class T, class Trim = std::u8string_view>
constexpr T&& ltrim(T&& str, Trim&& what = u8" \n")
{
	str.remove_prefix(std::min(str.find_first_not_of(what), str.size()));
	return str;
}
template<class T, class Trim = std::u8string_view>
constexpr T&& rtrim(T&& str, Trim&& what = u8" \n")
{
	if (auto pos = str.find_last_not_of(what); pos != str.npos) {
		str.remove_suffix(str.size() - pos - 1);
	}
	else {
		str.remove_suffix(str.size());
	}
	return str;
}
inline std::u8string trim(std::u8string&& str)
{
	//https://stackoverflow.com/a/44973498/524503
	str.erase(str.begin(), std::find_if(str.begin(), str.end(), [](unsigned char ch) {
		return ch != ' ' && ch != '\n';
		}));
	str.erase(std::find_if(str.rbegin(), str.rend(), [](unsigned char ch) {
		return ch != ' ' && ch != '\n';
		}).base(), str.end());
	return str;
}

inline std::string toNormal(std::u8string str) {
	return std::string{ reinterpret_cast<const char*>(str.data()),str.length() };
}

template<class Trim = std::u8string_view>
inline std::vector<std::u8string_view> explodeMul(std::u8string_view str, Trim splitOn) {
	std::vector<std::u8string_view> results;//a range-based soultion which rust gives a lazy iterator would be prefferable.
	while (!str.empty()) {
		ltrim(str, splitOn);//ignore this part on the left
		auto end = str.find_first_of(splitOn);
		auto split = str.substr(0, end);
		if (!split.empty()) {
			results.push_back(split);
			str.remove_prefix(split.size());
		}
	}
	return results;
}
