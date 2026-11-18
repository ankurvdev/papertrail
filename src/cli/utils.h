#pragma once
#include <filesystem>
#include <string>

std::string Sha256HashOfFile(std::filesystem::path const& filePath);
std::filesystem::path GetCacheDirectory();
