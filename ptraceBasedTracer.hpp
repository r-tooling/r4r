#pragma once
#include <unordered_map>
#include <memory>
#include <vector>
#include <vector>
#include "./middleend/middleEnd.hpp"

void doAnalysis(std::unordered_map<absFilePath, std::unique_ptr<middleend::MiddleEndState::file_info>>& fileInfos, std::vector<std::string>& origEnv, std::vector<std::string>& origArgs, std::filesystem::__cxx11::path& origWrkdir);
