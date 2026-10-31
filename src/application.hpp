#pragma once
#include <filesystem>
struct Options { bool smoke=false,validate=false; std::filesystem::path scene; };
int runApplication(const Options& options);
