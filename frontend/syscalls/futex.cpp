#include "futex.hpp"
#include <linux/futex.h>
namespace frontend::SyscallHandlers {

	void Futex::entry(processState& process, const middleend::MiddleEndState&, long)
	{
		auto op = process.getSyscallParam<2>();
		if (op == FUTEX_FD)
			fprintf(stderr, "Futex_FD is unhandled well \n");
		//(from Linux 2.6.0 up to and including Linux 2.6.25) 
		//TODO: maybe jus toss me in the bin and use an nullopt.
	}
}