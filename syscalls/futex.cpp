#include "futex.hpp"
#include <linux/futex.h>

void SyscallHandlers::Futex::entry(processState& process, const MiddleEndState& , long )
{
	auto op = getSyscallParam<2>(process.pid);
	assert(op != FUTEX_FD); //(from Linux 2.6.0 up to and including Linux 2.6.25) 
	//TODO: maybe jus toss me in the bin and use an nullopt.
}
