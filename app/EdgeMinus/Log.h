#pragma once

#include <string>

namespace edgeminus::log {

inline constexpr wchar_t kEnabledSettingKey[] = L"logsEnabled";

void Init(std::wstring const& localStateFolder, bool enabled);
void SetEnabled(bool enabled);
bool Enabled();
void Write(std::wstring const& line);
std::wstring Read();

}  // namespace edgeminus::log
