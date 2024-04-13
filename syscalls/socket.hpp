#pragma once
#include "genericSyscallHeader.hpp"
#include <sys/socket.h>
namespace SyscallHandlers {
	struct SocketBase {
		enum domain{
			local = AF_LOCAL,
			ipv4  = AF_INET,
			ipv6 = AF_INET6,
			other
			//todo: handle rest if they are interesting. But this should be plenty.

		} domain;
		void parseDomain(int userVal) {
			if (userVal == AF_LOCAL || userVal == AF_UNIX) {
				domain = local;
			}
			else if (userVal == AF_INET) {
				domain = ipv4;
			}
			else if (userVal == AF_INET6) {
				domain = ipv6;
			}
			else {
				domain = other;
			}
		}
		void printDomain(std::ostream& out) {
			out << "(domain " << ( (domain == local) ? "local" : (domain == ipv4) ? "ipv4" : (domain == ipv6) ? "ipv6" : "other" ) << ")";
		}
		//protocol should be unimportant at least for now.
		//type shoudl be unimportant
	};

	struct Connect : simpleSyscallHandler_base {
		fileDescriptor fd;
		long ptr;
		socklen_t structSize;

		void entry(processState& process, const MiddleEndState& state, long syscallNr) override;
		void exit(processState& process, MiddleEndState& state, long syscallRetval) override;
		void entryLog(const processState& process, const MiddleEndState& state, long syscallNr) override;
	};

	struct Socket : simpleSyscallHandler_base, SocketBase {
		// Inherited via simpleSyscallHandler_base
		void entry(processState& process, const MiddleEndState& state, long syscallNr) override;
		void exit(processState& process, MiddleEndState& state, long syscallRetval) override;
		void entryLog(const processState& process, const MiddleEndState& state, long syscallNr) override;
	};
	struct SocketPair : simpleSyscallHandler_base , SocketBase {
		long ptr;
		// Inherited via simpleSyscallHandler_base
		void entry(processState& process, const MiddleEndState& state, long syscallNr) override;
		void exit(processState& process, MiddleEndState& state, long syscallRetval) override;
		void entryLog(const processState& process, const MiddleEndState& state, long syscallNr) override;
	};
}

HandlerClassDef(SYS_connect) : public SyscallHandlers::Connect{};
HandlerClassDef(SYS_socket) : public SyscallHandlers::Socket{};
HandlerClassDef(SYS_socketpair) : public SyscallHandlers::SocketPair{};
NullOptHandlerClass(SYS_bind);
NullOptHandlerClass(SYS_poll);//TODO: consider checking the FD validities.