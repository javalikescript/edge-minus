#pragma once

#include <string>

namespace edgeminus::log {

void Init(std::wstring const& localStateFolder);
void Write(std::wstring const& line);

}  // namespace edgeminus::log
