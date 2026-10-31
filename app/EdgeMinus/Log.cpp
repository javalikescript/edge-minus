#include "pch.h"

#include "Log.h"

namespace edgeminus::log {
namespace {

std::mutex g_mutex;
FILE* g_file = nullptr;

}  // namespace

void Init(std::wstring const& localStateFolder) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_file) {
    std::fclose(g_file);
    g_file = nullptr;
  }
  const std::wstring path = localStateFolder + L"\\edgeminus.log";
  _wfopen_s(&g_file, path.c_str(), L"a, ccs=UTF-8");
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

}  // namespace edgeminus::log
