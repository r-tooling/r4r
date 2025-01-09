#pragma once
#include <filesystem>

namespace fs = std::filesystem;
using namespace std::string_literals;
using namespace std::string_view_literals;

#include <iostream>
#include <sstream>

#define STR(x) (((std::stringstream&)(std::stringstream() << x)).str())
