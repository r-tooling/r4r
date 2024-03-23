#pragma once
#include "genericSyscallHeader.hpp"
#include "syscallHeaders.hpp"
#include <unordered_map>
#include <memory>

namespace {

	std::unique_ptr<syscallHandler> createNullOpt() {
		return std::make_unique<errorHandler>(); //nullOptHandler
	}

	struct syscallHandlerMapper_base {
		std::unordered_map<int, decltype(createNullOpt)*> map;
		std::unique_ptr<syscallHandler> get(decltype(map)::key_type syscallNr) const {
			auto it = map.find(syscallNr);
			if (it != map.end()) {
				return (it->second)();
			}
			else {
				return createNullOpt();
			}
		}
	};

	template<typename, typename = void>
	constexpr bool is_type_complete_v = false;

	template<typename T>
	constexpr bool is_type_complete_v
		<T, std::void_t<decltype(sizeof(T))>> = true;

	template<int nr, int ... Rest>
	void filler(decltype(syscallHandlerMapper_base::map)& map) {
		map.emplace(nr, []() -> std::unique_ptr<syscallHandler> {return std::make_unique<simpleSyscallHandler<nr>>(); });
		if constexpr (sizeof...(Rest) == 0) {
			return;
		}
		else {
			filler<Rest...>(map);
		}
	};
	template<typename T, int>
	constexpr auto is_complete(int) -> decltype(sizeof(T), bool{}) {
		return true;
	}

	template<typename T, int>
	constexpr auto is_complete(...) -> bool {
		return false;
	}


#define IS_COMPLETE(T) is_complete<T,__COUNTER__>(0)
	static_assert(!IS_COMPLETE(simpleSyscallHandler<450>));

	//this magic esures the syscall handler map will  contain references to each syscall with a defined handler.
	//the fact that all handlers are added in is solved by the header ""syscallHeaders" which includes all the necessary headers in itself.
	template<int nr>
	void filler_desc(decltype(syscallHandlerMapper_base::map)& map) {
		if constexpr (IS_COMPLETE(simpleSyscallHandler<nr>)) {
			map.emplace(nr, []() -> std::unique_ptr<syscallHandler> {return std::make_unique<simpleSyscallHandler<nr>>(); });
		}
		if constexpr (nr < 0) {
			return;
		}
		else {
			filler_desc<nr-1>(map);
		}
	};

}

template<int ... Values>
struct syscallHandlerMapper :public syscallHandlerMapper_base {
	syscallHandlerMapper() {
		filler<Values...>(map);
	}
};

template<int maxNr>
struct syscallHandlerMapperOfAll :public syscallHandlerMapper_base {
	syscallHandlerMapperOfAll() {
		filler_desc<maxNr>(map);
	}
};

