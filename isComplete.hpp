#pragma once
/*
	Also known as "Does this template specialisation exist?" or "Was this defined"
*/
template<typename T, int>
constexpr auto is_complete(int) -> decltype(sizeof(T), bool{}) {
	return true;
}
/*
* Also known as "this was only forward declasred"
*/
template<typename T, int>
constexpr auto is_complete(...) -> bool {
	return false;
}

#define IS_COMPLETE(T) is_complete<T,__COUNTER__>(0)
