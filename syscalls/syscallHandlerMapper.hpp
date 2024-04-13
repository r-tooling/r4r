#pragma once
#include "genericSyscallHeader.hpp"
#include <unordered_map>
#include <memory>

namespace SyscallHandlers {

	inline std::unique_ptr<syscallHandler> createNullOpt() {
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
}

struct syscallHandlerMapperOfAll :public SyscallHandlers::syscallHandlerMapper_base {
	syscallHandlerMapperOfAll();
};

