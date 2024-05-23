#include "syscallHandlerMapperInline.hpp"

#define DYNAMIC_INVOKE(NR,NAME,...) do{ \
switch(NR){ \
case 0: NAME<TemplatedSyscallHandler<0>>( __VA_ARGS__ ); break; \
 case 1: NAME<TemplatedSyscallHandler<1>>( __VA_ARGS__ ); break; \
 case 2: NAME<TemplatedSyscallHandler<2>>( __VA_ARGS__ ); break; \
 case 5: NAME<TemplatedSyscallHandler<5>>( __VA_ARGS__ ); break; \
 case 6: NAME<TemplatedSyscallHandler<6>>( __VA_ARGS__ ); break; \
 case 7: NAME<TemplatedSyscallHandler<7>>( __VA_ARGS__ ); break; \
 case 12: NAME<TemplatedSyscallHandler<12>>( __VA_ARGS__ ); break; \
 case 14: NAME<TemplatedSyscallHandler<14>>( __VA_ARGS__ ); break; \
 case 16: NAME<TemplatedSyscallHandler<16>>( __VA_ARGS__ ); break; \
 case 17: NAME<TemplatedSyscallHandler<17>>( __VA_ARGS__ ); break; \
 case 18: NAME<TemplatedSyscallHandler<18>>( __VA_ARGS__ ); break; \
 case 21: NAME<TemplatedSyscallHandler<21>>( __VA_ARGS__ ); break; \
 case 22: NAME<TemplatedSyscallHandler<22>>( __VA_ARGS__ ); break; \
 case 24: NAME<TemplatedSyscallHandler<24>>( __VA_ARGS__ ); break; \
 case 28: NAME<TemplatedSyscallHandler<28>>( __VA_ARGS__ ); break; \
 case 41: NAME<TemplatedSyscallHandler<41>>( __VA_ARGS__ ); break; \
 case 42: NAME<TemplatedSyscallHandler<42>>( __VA_ARGS__ ); break; \
 case 49: NAME<TemplatedSyscallHandler<49>>( __VA_ARGS__ ); break; \
 case 53: NAME<TemplatedSyscallHandler<53>>( __VA_ARGS__ ); break; \
 case 57: NAME<TemplatedSyscallHandler<57>>( __VA_ARGS__ ); break; \
 case 59: NAME<TemplatedSyscallHandler<59>>( __VA_ARGS__ ); break; \
 case 60: NAME<TemplatedSyscallHandler<60>>( __VA_ARGS__ ); break; \
 case 61: NAME<TemplatedSyscallHandler<61>>( __VA_ARGS__ ); break; \
 case 63: NAME<TemplatedSyscallHandler<63>>( __VA_ARGS__ ); break; \
 case 72: NAME<TemplatedSyscallHandler<72>>( __VA_ARGS__ ); break; \
 case 79: NAME<TemplatedSyscallHandler<79>>( __VA_ARGS__ ); break; \
 case 82: NAME<TemplatedSyscallHandler<82>>( __VA_ARGS__ ); break; \
 case 83: NAME<TemplatedSyscallHandler<83>>( __VA_ARGS__ ); break; \
 case 84: NAME<TemplatedSyscallHandler<84>>( __VA_ARGS__ ); break; \
 case 85: NAME<TemplatedSyscallHandler<85>>( __VA_ARGS__ ); break; \
 case 87: NAME<TemplatedSyscallHandler<87>>( __VA_ARGS__ ); break; \
 case 89: NAME<TemplatedSyscallHandler<89>>( __VA_ARGS__ ); break; \
 case 97: NAME<TemplatedSyscallHandler<97>>( __VA_ARGS__ ); break; \
 case 98: NAME<TemplatedSyscallHandler<98>>( __VA_ARGS__ ); break; \
 case 13: NAME<TemplatedSyscallHandler<13>>( __VA_ARGS__ ); break; \
 case 267: NAME<TemplatedSyscallHandler<267>>( __VA_ARGS__ ); break; \
 case 15: NAME<TemplatedSyscallHandler<15>>( __VA_ARGS__ ); break; \
 case 269: NAME<TemplatedSyscallHandler<269>>( __VA_ARGS__ ); break; \
 case 283: NAME<TemplatedSyscallHandler<283>>( __VA_ARGS__ ); break; \
 case 284: NAME<TemplatedSyscallHandler<284>>( __VA_ARGS__ ); break; \
 case 58: NAME<TemplatedSyscallHandler<58>>( __VA_ARGS__ ); break; \
 case 439: NAME<TemplatedSyscallHandler<439>>( __VA_ARGS__ ); break; \
 case 8: NAME<TemplatedSyscallHandler<8>>( __VA_ARGS__ ); break; \
 case 262: NAME<TemplatedSyscallHandler<262>>( __VA_ARGS__ ); break; \
 case 290: NAME<TemplatedSyscallHandler<290>>( __VA_ARGS__ ); break; \
 case 113: NAME<TemplatedSyscallHandler<113>>( __VA_ARGS__ ); break; \
 case 318: NAME<TemplatedSyscallHandler<318>>( __VA_ARGS__ ); break; \
 case 107: NAME<TemplatedSyscallHandler<107>>( __VA_ARGS__ ); break; \
 case 234: NAME<TemplatedSyscallHandler<234>>( __VA_ARGS__ ); break; \
 case 39: NAME<TemplatedSyscallHandler<39>>( __VA_ARGS__ ); break; \
 case 293: NAME<TemplatedSyscallHandler<293>>( __VA_ARGS__ ); break; \
 case 77: NAME<TemplatedSyscallHandler<77>>( __VA_ARGS__ ); break; \
 case 204: NAME<TemplatedSyscallHandler<204>>( __VA_ARGS__ ); break; \
 case 56: NAME<TemplatedSyscallHandler<56>>( __VA_ARGS__ ); break; \
 case 437: NAME<TemplatedSyscallHandler<437>>( __VA_ARGS__ ); break; \
 case 292: NAME<TemplatedSyscallHandler<292>>( __VA_ARGS__ ); break; \
 case 106: NAME<TemplatedSyscallHandler<106>>( __VA_ARGS__ ); break; \
 case 233: NAME<TemplatedSyscallHandler<233>>( __VA_ARGS__ ); break; \
 case 247: NAME<TemplatedSyscallHandler<247>>( __VA_ARGS__ ); break; \
 case 80: NAME<TemplatedSyscallHandler<80>>( __VA_ARGS__ ); break; \
 case 334: NAME<TemplatedSyscallHandler<334>>( __VA_ARGS__ ); break; \
 case 20: NAME<TemplatedSyscallHandler<20>>( __VA_ARGS__ ); break; \
 case 274: NAME<TemplatedSyscallHandler<274>>( __VA_ARGS__ ); break; \
 case 78: NAME<TemplatedSyscallHandler<78>>( __VA_ARGS__ ); break; \
 case 332: NAME<TemplatedSyscallHandler<332>>( __VA_ARGS__ ); break; \
 case 273: NAME<TemplatedSyscallHandler<273>>( __VA_ARGS__ ); break; \
 case 435: NAME<TemplatedSyscallHandler<435>>( __VA_ARGS__ ); break; \
 case 316: NAME<TemplatedSyscallHandler<316>>( __VA_ARGS__ ); break; \
 case 3: NAME<TemplatedSyscallHandler<3>>( __VA_ARGS__ ); break; \
 case 257: NAME<TemplatedSyscallHandler<257>>( __VA_ARGS__ ); break; \
 case 291: NAME<TemplatedSyscallHandler<291>>( __VA_ARGS__ ); break; \
 case 114: NAME<TemplatedSyscallHandler<114>>( __VA_ARGS__ ); break; \
 case 302: NAME<TemplatedSyscallHandler<302>>( __VA_ARGS__ ); break; \
 case 32: NAME<TemplatedSyscallHandler<32>>( __VA_ARGS__ ); break; \
 case 286: NAME<TemplatedSyscallHandler<286>>( __VA_ARGS__ ); break; \
 case 109: NAME<TemplatedSyscallHandler<109>>( __VA_ARGS__ ); break; \
 case 230: NAME<TemplatedSyscallHandler<230>>( __VA_ARGS__ ); break; \
 case 110: NAME<TemplatedSyscallHandler<110>>( __VA_ARGS__ ); break; \
 case 218: NAME<TemplatedSyscallHandler<218>>( __VA_ARGS__ ); break; \
 case 217: NAME<TemplatedSyscallHandler<217>>( __VA_ARGS__ ); break; \
 case 158: NAME<TemplatedSyscallHandler<158>>( __VA_ARGS__ ); break; \
 case 99: NAME<TemplatedSyscallHandler<99>>( __VA_ARGS__ ); break; \
 case 213: NAME<TemplatedSyscallHandler<213>>( __VA_ARGS__ ); break; \
 case 202: NAME<TemplatedSyscallHandler<202>>( __VA_ARGS__ ); break; \
 case 161: NAME<TemplatedSyscallHandler<161>>( __VA_ARGS__ ); break; \
 case 102: NAME<TemplatedSyscallHandler<102>>( __VA_ARGS__ ); break; \
 case 33: NAME<TemplatedSyscallHandler<33>>( __VA_ARGS__ ); break; \
 case 160: NAME<TemplatedSyscallHandler<160>>( __VA_ARGS__ ); break; \
 case 228: NAME<TemplatedSyscallHandler<228>>( __VA_ARGS__ ); break; \
 case 101: NAME<TemplatedSyscallHandler<101>>( __VA_ARGS__ ); break; \
 case 11: NAME<TemplatedSyscallHandler<11>>( __VA_ARGS__ ); break; \
 case 138: NAME<TemplatedSyscallHandler<138>>( __VA_ARGS__ ); break; \
 case 10: NAME<TemplatedSyscallHandler<10>>( __VA_ARGS__ ); break; \
 case 264: NAME<TemplatedSyscallHandler<264>>( __VA_ARGS__ ); break; \
 case 137: NAME<TemplatedSyscallHandler<137>>( __VA_ARGS__ ); break; \
 case 9: NAME<TemplatedSyscallHandler<9>>( __VA_ARGS__ ); break; \
 case 263: NAME<TemplatedSyscallHandler<263>>( __VA_ARGS__ ); break; \
 case 136: NAME<TemplatedSyscallHandler<136>>( __VA_ARGS__ ); break; \
 case 4: NAME<TemplatedSyscallHandler<4>>( __VA_ARGS__ ); break; \
 case 258: NAME<TemplatedSyscallHandler<258>>( __VA_ARGS__ ); break; \
 case 131: NAME<TemplatedSyscallHandler<131>>( __VA_ARGS__ ); break; \
 case 121: NAME<TemplatedSyscallHandler<121>>( __VA_ARGS__ ); break; \
 case 119: NAME<TemplatedSyscallHandler<119>>( __VA_ARGS__ ); break; \
 case 117: NAME<TemplatedSyscallHandler<117>>( __VA_ARGS__ ); break; \
 case 111: NAME<TemplatedSyscallHandler<111>>( __VA_ARGS__ ); break; \
 case 108: NAME<TemplatedSyscallHandler<108>>( __VA_ARGS__ ); break; \
 case 232: NAME<TemplatedSyscallHandler<232>>( __VA_ARGS__ ); break; \
 case 105: NAME<TemplatedSyscallHandler<105>>( __VA_ARGS__ ); break; \
 case 231: NAME<TemplatedSyscallHandler<231>>( __VA_ARGS__ ); break; \
 case 104: NAME<TemplatedSyscallHandler<104>>( __VA_ARGS__ ); break; \
 default: NAME<ErrorHandler>( __VA_ARGS__ );\
}\
}while(0);

namespace frontend::SyscallHandlers {
	void HandlerWrapper::destroy() noexcept {
		if (syscallNr.has_value()) {
			DYNAMIC_INVOKE(syscallNr.value(), destructorInvoke)
			syscallNr = std::nullopt;
		}
	}

	 void HandlerWrapper::create(size_t newNr) noexcept {
		destroy();
		syscallNr = newNr;
		DYNAMIC_INVOKE(syscallNr.value(), constructorInvoke)
	}

	 void HandlerWrapper::entry(processState& a, const middleend::MiddleEndState& b, long c)
	 {
		 DYNAMIC_INVOKE(syscallNr.value(), _entry, a, b, c)
	 }

	 void HandlerWrapper::exit(processState& a, middleend::MiddleEndState& b, long c)
	 {
		 DYNAMIC_INVOKE(syscallNr.value(), _exit, a, b, c)
	 }

	 void HandlerWrapper::entryLog(const processState& a, const middleend::MiddleEndState& b, long c)
	 {
		 DYNAMIC_INVOKE(syscallNr.value(), _entryLog, a, b, c)
	 }

	 void HandlerWrapper::exitLog(const processState& a, const middleend::MiddleEndState& b , long c)
	 {
		 DYNAMIC_INVOKE(syscallNr.value(), _exitLog, a, b, c)
	 }

	template<class T>
	 void HandlerWrapper::destructorInvoke() noexcept {
		static_assert(std::is_nothrow_destructible_v<T>);
		std::launder(reinterpret_cast<T*>(buffer))->~T();
	}
	template<class T>
	 void HandlerWrapper::constructorInvoke() noexcept {
		//static_assert(std::is_nothrow_constructible_v<T>)); TODO: the stringstreams break this :/
		::new (std::launder(reinterpret_cast<T*>(buffer))) T {  };
	}

	template<class T>
	void HandlerWrapper::_entry(processState& a,const middleend::MiddleEndState& b, long c) {
		std::launder(reinterpret_cast<T*>(buffer))->entry(a, b, c);
	}
	template<class T>
	void HandlerWrapper::_entryLog(const processState& a, const middleend::MiddleEndState& b, long c) {
		std::launder(reinterpret_cast<T*>(buffer))->entryLog(a, b, c);
	}
	template<class T>
	void HandlerWrapper::_exit( processState& a, middleend::MiddleEndState& b, long c) {
		std::launder(reinterpret_cast<T*>(buffer))->exit(a, b, c);
	}
	template<class T>
	void HandlerWrapper::_exitLog(const processState& a,const middleend::MiddleEndState& b, long c) {
		std::launder(reinterpret_cast<T*>(buffer))->exitLog(a, b, c);
	}
}