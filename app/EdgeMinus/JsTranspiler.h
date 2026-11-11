#pragma once

#include <string>

namespace edgeminus::content {

bool LooksLikeModernScript(std::wstring const& source);
std::wstring TranspileJavaScript(std::wstring const& source);

}  // namespace edgeminus::content
