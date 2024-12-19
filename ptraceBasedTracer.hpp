#pragma once
#include "./middleend/middleEnd.hpp"
#include <memory>
#include <unordered_map>
#include <vector>

void doAnalysis(
    std::unordered_map<absFilePath,
                       std::unique_ptr<middleend::MiddleEndState::file_info>>&
        fileInfos,
    std::vector<std::string>& origEnv, std::vector<std::string>& origArgs,
    std::filesystem::__cxx11::path& origWrkdir);

void LoadAndAnalyse();
