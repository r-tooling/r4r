#pragma once 
#include <string>
#include <string_view>


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
