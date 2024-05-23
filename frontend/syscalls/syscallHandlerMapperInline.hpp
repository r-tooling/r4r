#pragma once
#include "genericSyscallHeader.hpp"
#include "syscallHeaders.hpp"
#include "../../isComplete.hpp"
#include <unordered_map>
#include <memory>
#include <optional>
namespace frontend::SyscallHandlers {

	template<size_t nr = MaxSyscallNr >
	constexpr size_t alignofAllTemplates() {
		if constexpr (nr <= 0)
			return alignof(ErrorHandler);
		else if constexpr (IS_COMPLETE(TemplatedSyscallHandler<nr>))
			return std::max(alignof(TemplatedSyscallHandler<nr>), alignofAllTemplates<nr - 1>());
		else
			return alignofAllTemplates<nr - 1>();
	}
	template<size_t nr = MaxSyscallNr >
	constexpr size_t sizeofAllTemplates() {
		if constexpr (nr <= 0)
			return sizeof(ErrorHandler);
		else if constexpr (IS_COMPLETE(TemplatedSyscallHandler<nr>))
			return std::max(sizeof(TemplatedSyscallHandler<nr>), sizeofAllTemplates<nr - 1>());
		else
			return sizeofAllTemplates<nr - 1>();
	}

	struct HandlerWrapper {

		//no moving or copies allowed
		HandlerWrapper() = default;
		HandlerWrapper(const HandlerWrapper&) = delete;
		HandlerWrapper(HandlerWrapper&&) = delete;
		 
		~HandlerWrapper() noexcept {
			destroy();
		}
		void destroy() noexcept;
		void create(size_t newNr) noexcept;
		void entry(processState&, const middleend::MiddleEndState&, long);
		void exit(processState&, middleend::MiddleEndState&, long);

		void entryLog(const processState&, const middleend::MiddleEndState&, long);
		void exitLog(const processState&, const middleend::MiddleEndState&, long);

		explicit operator bool() const noexcept{
			return syscallNr.has_value();
		}
	private:
		std::optional<int> syscallNr = std::nullopt;
		alignas(alignofAllTemplates()) std::byte buffer[sizeofAllTemplates()]; //visual studio and other IDS may complain slightly here. Ignore that, it compiles just fine as it is.

		template<class T>
		void destructorInvoke() noexcept;
		template<class T>
		void constructorInvoke() noexcept;
		//now this would be amazing to do via templates. It cannot be done as the template can take a function offset but not a function name.
		template<class T>
		void _entry(processState& a, const middleend::MiddleEndState& b, long c);
		template<class T>
		void _entryLog(const processState& a,const middleend::MiddleEndState& b, long c);
		template<class T>
		void _exit(processState& a, middleend::MiddleEndState& b, long c);
		template<class T>
		void _exitLog(const processState& a,const middleend::MiddleEndState& b, long c);
	};


	/*
		It is wrapped in a class to allow for potential ignoring of some syscalls at runtime.
	*/
	struct SyscallHandlerMapperInline {
		void get(int syscallNr, HandlerWrapper& wrapper) const {
			wrapper.destroy();
			wrapper.create(syscallNr);
		}
	};
}