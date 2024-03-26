#include "syscallHandlerMapper.hpp"
#include "../syscallMapping.hpp"

namespace {
	using namespace SyscallHandlers;

	template<typename T, int>
	constexpr auto is_complete(int) -> decltype(sizeof(T), bool{}) {
		return true;
	}

	template<typename T, int>
	constexpr auto is_complete(...) -> bool {
		return false;
	}


#define IS_COMPLETE(T) is_complete<T,__COUNTER__>(0)
	static_assert(!IS_COMPLETE(simpleSyscallHandler<MaxSyscallNr + 1>));
	//this magic esures the syscall handler map will  contain references to each syscall with a defined handler.
//the fact that all handlers are added in is solved by the header ""syscallHeaders" which includes all the necessary headers in itself.
	template<int nr = MaxSyscallNr>
	void filler_desc(decltype(syscallHandlerMapper_base::map)& map) {
		if constexpr (IS_COMPLETE(simpleSyscallHandler<nr>)) {
			map.emplace(nr, []() -> std::unique_ptr<syscallHandler> {return std::make_unique<simpleSyscallHandler<nr>>(); });
		}
		if constexpr (nr < 0) {
			return;
		}
		else {
			filler_desc<nr - 1>(map);
		}
	};
}

syscallHandlerMapperOfAll::syscallHandlerMapperOfAll()
{
	filler_desc(map);
}
