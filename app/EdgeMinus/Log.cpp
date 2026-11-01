#include "pch.h"

#include "Log.h"

namespace edgeminus::log {
namespace {

std::mutex g_mutex;
FILE* g_file = nullptr;
std::wstring g_path;

}  // namespace

void Init(std::wstring const& localStateFolder) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_file) {
    std::fclose(g_file);
    g_file = nullptr;
  }
  g_path = localStateFolder + L"\\edgeminus.log";
  _wfopen_s(&g_file, g_path.c_str(), L"a, ccs=UTF-8");
}

void Write(std::wstring const& line) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_file) {
    return;
  }
  SYSTEMTIME now{};
  ::GetLocalTime(&now);
  std::fwprintf(g_file, L"[%02u:%02u:%02u.%03u] %ls\n", now.wHour, now.wMinute,
                now.wSecond, now.wMilliseconds, line.c_str());
  std::fflush(g_file);
}

std::wstring Read() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_path.empty()) {
    return {};
  }

  FILE* file = nullptr;
  if (_wfopen_s(&file, g_path.c_str(), L"r, ccs=UTF-8") != 0 || !file) {
    return {};
  }

  std::wstring text;
  wchar_t buffer[4096];
  while (std::fgetws(
      buffer, static_cast<int>(sizeof(buffer) / sizeof(buffer[0])), file)) {
    text += buffer;
  }
  std::fclose(file);

  if (!text.empty() && text.front() == L'\uFEFF') {
    text.erase(text.begin());
  }
  return text;
}

}  // namespace edgeminus::log
