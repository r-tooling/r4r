
#include "frontend/syscalls/syscallHeaders.hpp"
#include "isComplete.hpp"
#include <iostream>


template<int nr = MaxSyscallNr>
void filler_desc_name(std::unordered_map < size_t, std::string >& map) {
	if constexpr (IS_COMPLETE(frontend::SyscallHandlers::TemplatedSyscallHandler<nr>)) {
		map.emplace(nr, "TemplatedSyscallHandler<" + std::to_string(nr) + ">");
	}
	if constexpr (nr < 0) {
		return;
	}
	else {
		filler_desc_name<nr - 1>(map);
	}
};

std::string createDispatchMacro()
{
	std::stringstream result;

	std::unordered_map < size_t, std::string > nrToName;
	filler_desc_name(nrToName);
	result << "#define DYNAMIC_INVOKE(NR,NAME,...) do{ \\\n"
		"switch(NR){ \\\n";
	for (auto& [nr, type] : nrToName) {
		result << "case " << nr << ": NAME<" << type << ">( __VA_ARGS__ ); break; \\\n ";
	}
	result << "default: NAME<ErrorHandler>( __VA_ARGS__ );\\\n"
		"}\\\n"
		"}while(0);";

	return result.str();
}

int main(int argc, char* argv[])
{
	std::cout << createDispatchMacro() << std::endl;
	return 0;
}