#pragma once
#include "genericSyscallHeader.hpp"
#include <memory>
#include <unordered_map>
namespace frontend::SyscallHandlers {
/*
        Create a handler for undefined syscalls
*/
inline std::unique_ptr<SyscallHandler> createNullOpt() {
    return std::make_unique<ErrorHandler>(); // nullOptHandler
}
/*
        The base functionality of the mapper between syscalls and thir handlers.
*/
struct SyscallHandlerMapper_base {
    std::unordered_map<int, decltype(createNullOpt)*> map;
    std::unique_ptr<SyscallHandler>
    get(decltype(map)::key_type syscallNr) const {
        auto it = map.find(syscallNr);
        if (it != map.end()) {
            return (it->second)();
        } else {
            return createNullOpt();
        }
    }
};
/*
        THe constructor will fill the base map with all known syscalls
*/
struct SyscallHandlerMapperOfAll : public SyscallHandlerMapper_base {
    SyscallHandlerMapperOfAll();
};
} // namespace frontend::SyscallHandlers