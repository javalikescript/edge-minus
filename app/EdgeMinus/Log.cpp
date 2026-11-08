#include "pch.h"

#include "Log.h"

namespace edgeminus::log {
namespace {

std::mutex g_mutex;
FILE* g_file = nullptr;
std::wstring g_path;
bool g_enabled = false;

void CloseFile() {
  if (g_file) {
    std::fclose(g_file);
    g_file = nullptr;
  }
}

void OpenFile(bool truncate) {
  if (g_path.empty()) {
    return;
  }
  _wfopen_s(&g_file, g_path.c_str(),
            truncate ? L"w, ccs=UTF-8" : L"a, ccs=UTF-8");
}

}  // namespace

void Init(std::wstring const& localStateFolder, bool enabled) {
  std::lock_guard<std::mutex> lock(g_mutex);
  CloseFile();
  g_path = localStateFolder + L"\\edgeminus.log";
  g_enabled = enabled;
  if (g_enabled) {
    OpenFile(true);
  }
}

void SetEnabled(bool enabled) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_enabled == enabled) {
    return;
  }
  CloseFile();
  g_enabled = enabled;
  if (g_enabled) {
    OpenFile(true);
  }
}

bool Enabled() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_enabled;
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
  if (g_path.empty() || !g_enabled) {
    return {};
  }
  CloseFile();

  std::wstring text = {};
  FILE* file = nullptr;
  if (!_wfopen_s(&file, g_path.c_str(), L"r, ccs=UTF-8") != 0 && file) {
    wchar_t buffer[4096];
    while (std::fgetws(
        buffer, static_cast<int>(sizeof(buffer) / sizeof(buffer[0])), file)) {
      text += buffer;
    }
    std::fclose(file);
  
    if (!text.empty() && text.front() == L'\uFEFF') {
      text.erase(text.begin());
    }
  }

  OpenFile(false);
  return text;
}

}  // namespace edgeminus::log
