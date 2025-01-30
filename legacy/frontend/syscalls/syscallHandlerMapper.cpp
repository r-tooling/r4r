#include "syscallHandlerMapper.hpp"
#include "../../isComplete.hpp"
#include "../syscallMapping.hpp"
#include "syscallHeaders.hpp"

namespace {
using namespace frontend;
using namespace frontend::SyscallHandlers;
static_assert(!IS_COMPLETE(TemplatedSyscallHandler<MaxSyscallNr + 1>));
// this magic esures the syscall handler map will  contain references to each
// syscall with a defined handler.
// the fact that all handlers are added in is solved by the header
// ""syscallHeaders" which includes all the necessary headers in itself.
template <int nr = MaxSyscallNr>
void filler_desc(decltype(SyscallHandlerMapper_base::map)& map) {
    if constexpr (IS_COMPLETE(TemplatedSyscallHandler<nr>)) {
        map.emplace(nr, []() -> std::unique_ptr<SyscallHandler> {
            return std::make_unique<TemplatedSyscallHandler<nr>>();
        });
    }
    if constexpr (nr < 0) {
        return;
    } else {
        filler_desc<nr - 1>(map);
    }
};
} // namespace
namespace frontend::SyscallHandlers {
SyscallHandlerMapperOfAll::SyscallHandlerMapperOfAll() { filler_desc(map); }
} // namespace frontend::SyscallHandlers