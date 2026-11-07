#include "pch.h"

#include "Extensions.h"

#include "Log.h"
#include "third_party/miniz/miniz.h"

#include <combaseapi.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <exception>
#include <vector>

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Storage;

namespace edgeminus::extensions {
namespace {

constexpr wchar_t kExtensionsFolderName[] = L"extensions";
constexpr wchar_t kUserscriptFileName[] = L"userscript.js";
constexpr wchar_t kIndexFileName[] = L"index.html";
constexpr uint64_t kMaxZipBytes = 64ull * 1024ull * 1024ull;
constexpr uint64_t kMaxExtensionBytes = 64ull * 1024ull * 1024ull;
constexpr size_t kMaxEntries = 4096;

[[noreturn]] void Fail(const wchar_t* message) {
  throw hresult_error(E_INVALIDARG, hstring(message));
}

std::wstring Trim(std::wstring text) {
  const size_t first = text.find_first_not_of(L" \t\r\n");
  if (first == std::wstring::npos) {
    return {};
  }
  const size_t last = text.find_last_not_of(L" \t\r\n");
  return text.substr(first, last - first + 1);
}

bool EndsWithIgnoreCase(std::wstring const& text, std::wstring_view suffix) {
  return text.size() >= suffix.size() &&
         _wcsicmp(text.c_str() + text.size() - suffix.size(), suffix.data()) == 0;
}

std::wstring JsString(std::wstring const& text) {
  std::wstring result = L"\"";
  for (wchar_t ch : text) {
    switch (ch) {
      case L'\\':
        result += L"\\\\";
        break;
      case L'"':
        result += L"\\\"";
        break;
      case L'\n':
        result += L"\\n";
        break;
      case L'\r':
        result += L"\\r";
        break;
      case L'\t':
        result += L"\\t";
        break;
      default:
        if (ch < 0x20) {
          wchar_t escaped[8];
          swprintf_s(escaped, L"\\u%04x", static_cast<unsigned>(ch));
          result += escaped;
        } else {
          result += ch;
        }
        break;
    }
  }
  result += L'"';
  return result;
}

bool WildcardMatch(std::wstring const& pattern, std::wstring const& text,
                   bool caseInsensitive) {
  size_t patternIndex = 0;
  size_t textIndex = 0;
  size_t star = std::wstring::npos;
  size_t mark = 0;

  auto equal = [caseInsensitive](wchar_t left, wchar_t right) {
    return caseInsensitive
               ? std::towlower(left) == std::towlower(right)
               : left == right;
  };

  while (textIndex < text.size()) {
    if (patternIndex < pattern.size() &&
        (pattern[patternIndex] == L'?' ||
         equal(pattern[patternIndex], text[textIndex]))) {
      ++patternIndex;
      ++textIndex;
    } else if (patternIndex < pattern.size() &&
               pattern[patternIndex] == L'*') {
      star = patternIndex++;
      mark = textIndex;
    } else if (star != std::wstring::npos) {
      patternIndex = star + 1;
      textIndex = ++mark;
    } else {
      return false;
    }
  }

  while (patternIndex < pattern.size() && pattern[patternIndex] == L'*') {
    ++patternIndex;
  }
  return patternIndex == pattern.size();
}

struct UrlParts {
  std::wstring scheme;
  std::wstring host;
  std::wstring path;
};

UrlParts SplitUrl(std::wstring const& value) {
  UrlParts parts;
  const size_t schemeEnd = value.find(L"://");
  if (schemeEnd == std::wstring::npos) {
    parts.path = value;
    return parts;
  }

  parts.scheme = value.substr(0, schemeEnd);
  const size_t authorityStart = schemeEnd + 3;
  const size_t pathStart = value.find_first_of(L"/?#", authorityStart);
  const size_t authorityEnd =
      pathStart == std::wstring::npos ? value.size() : pathStart;
  std::wstring authority =
      value.substr(authorityStart, authorityEnd - authorityStart);

  const size_t userInfo = authority.rfind(L'@');
  if (userInfo != std::wstring::npos) {
    authority = authority.substr(userInfo + 1);
  }

  if (!authority.empty() && authority.front() == L'[') {
    const size_t bracket = authority.find(L']');
    parts.host = bracket == std::wstring::npos
                     ? authority
                     : authority.substr(0, bracket + 1);
  } else {
    const size_t colon = authority.rfind(L':');
    parts.host =
        colon == std::wstring::npos ? authority : authority.substr(0, colon);
  }

  if (pathStart == std::wstring::npos) {
    parts.path = L"/";
  } else {
    parts.path = value.substr(pathStart);
    const size_t fragment = parts.path.find(L'#');
    if (fragment != std::wstring::npos) {
      parts.path.resize(fragment);
    }
  }
  return parts;
}

void AddPattern(std::vector<std::wstring>& patterns, std::wstring value) {
  value = Trim(std::move(value));
  if (value.empty()) {
    return;
  }
  for (auto const& existing : patterns) {
    if (_wcsicmp(existing.c_str(), value.c_str()) == 0) {
      return;
    }
  }
  patterns.push_back(std::move(value));
}

void ParseUserscriptMatches(std::wstring const& code,
                            std::vector<std::wstring>& patterns) {
  constexpr wchar_t kMatchTag[] = L"@match";
  constexpr size_t kMatchTagLength = 6;

  size_t start = 0;
  while (start < code.size()) {
    size_t end = code.find(L'\n', start);
    if (end == std::wstring::npos) {
      end = code.size();
    }
    std::wstring line = Trim(code.substr(start, end - start));
    start = end + 1;
    if (line.empty()) {
      continue;
    }

    if (line.rfind(L"//", 0) == 0) {
      line = Trim(line.substr(2));
    } else if (line.rfind(L"/*", 0) == 0) {
      line = Trim(line.substr(2));
    } else if (line.front() == L'*') {
      line = Trim(line.substr(1));
    } else {
      continue;
    }

    if (line.size() >= 2 && line.compare(line.size() - 2, 2, L"*/") == 0) {
      line = Trim(line.substr(0, line.size() - 2));
    }

    if (line.size() < kMatchTagLength ||
        _wcsnicmp(line.c_str(), kMatchTag, kMatchTagLength) != 0) {
      continue;
    }
    if (line.size() > kMatchTagLength &&
        !std::iswspace(line[kMatchTagLength])) {
      continue;
    }
    AddPattern(patterns, Trim(line.substr(kMatchTagLength)));
  }
}

std::wstring MakeShortId() {
  log::Write(L"extension: generating id");
  GUID guid{};
  if (FAILED(CoCreateGuid(&guid))) {
    log::Write(L"extension: guid generation failed");
    return L"00000000";
  }

  wchar_t buffer[9]{};
  swprintf_s(buffer, 9, L"%08lx", static_cast<unsigned long>(guid.Data1));
  const std::wstring id(buffer);
  log::Write(L"extension: generated id " + id);
  return id;
}

std::wstring DecodeEntryName(const char* bytes) {
  const size_t length = std::strlen(bytes);
  if (length == 0) {
    return {};
  }
  const int byteCount = static_cast<int>(length);

  int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes,
                                   byteCount, nullptr, 0);
  UINT codePage = CP_UTF8;
  DWORD flags = MB_ERR_INVALID_CHARS;
  if (needed <= 0) {
    codePage = CP_ACP;
    flags = 0;
    needed = MultiByteToWideChar(codePage, flags, bytes, byteCount, nullptr, 0);
  }
  if (needed <= 0) {
    return {};
  }

  std::wstring result(static_cast<size_t>(needed), L'\0');
  MultiByteToWideChar(codePage, flags, bytes, byteCount, result.data(), needed);
  return result;
}

std::wstring DecodeText(std::vector<uint8_t> const& bytes) {
  const size_t length = bytes.size();
  if (length == 0) {
    return {};
  }

  auto decode = [&bytes, length](UINT codePage, DWORD flags,
                                 size_t offset) -> std::wstring {
    if (offset >= length) {
      return {};
    }
    const char* data = reinterpret_cast<const char*>(bytes.data() + offset);
    const int byteCount = static_cast<int>(length - offset);
    int needed = MultiByteToWideChar(codePage, flags, data, byteCount, nullptr, 0);
    if (needed <= 0) {
      return {};
    }
    std::wstring result(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(codePage, flags, data, byteCount, result.data(), needed);
    return result;
  };

  if (length >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
    const size_t units = (length - 2) / 2;
    std::wstring result(units, L'\0');
    if (units != 0) {
      std::memcpy(result.data(), bytes.data() + 2, units * 2);
    }
    return result;
  }
  if (length >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) {
    const size_t units = (length - 2) / 2;
    std::wstring result(units, L'\0');
    for (size_t index = 0; index < units; ++index) {
      result[index] = static_cast<wchar_t>((bytes[2 + index * 2] << 8) |
                                           bytes[3 + index * 2]);
    }
    return result;
  }

  size_t offset = 0;
  if (length >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB &&
      bytes[2] == 0xBF) {
    offset = 3;
  }
  std::wstring text = decode(CP_UTF8, MB_ERR_INVALID_CHARS, offset);
  if (!text.empty() || length == offset) {
    return text;
  }
  return decode(CP_ACP, 0, offset);
}

std::wstring NormalizeEntryPath(std::wstring const& raw) {
  std::wstring path;
  for (wchar_t ch : raw) {
    path.push_back(ch == L'\\' ? L'/' : ch);
  }
  if (path.empty() || path.front() == L'/') {
    return {};
  }
  if (path.size() >= 2 && path[1] == L':') {
    return {};
  }

  std::wstring normalized;
  size_t start = 0;
  while (start <= path.size()) {
    const size_t slash = path.find(L'/', start);
    const bool last = slash == std::wstring::npos;
    const std::wstring segment =
        last ? path.substr(start) : path.substr(start, slash - start);

    if (!segment.empty()) {
      if (segment == L"." || segment == L"..") {
        return {};
      }
      for (wchar_t ch : segment) {
        if (ch < 0x20 || ch == L'<' || ch == L'>' || ch == L':' ||
            ch == L'"' || ch == L'|' || ch == L'?' || ch == L'*') {
          return {};
        }
      }
      if (segment.back() == L' ' || segment.back() == L'.') {
        return {};
      }
      if (!normalized.empty()) {
        normalized.push_back(L'/');
      }
      normalized += segment;
    }

    if (last) {
      break;
    }
    start = slash + 1;
  }
  return normalized;
}

class ZipReader {
 public:
  explicit ZipReader(std::vector<uint8_t> const& bytes) {
    if (!mz_zip_reader_init_mem(&archive_, bytes.data(), bytes.size(), 0)) {
      Fail(L"The file is not a valid zip archive.");
    }
    open_ = true;
  }

  ~ZipReader() {
    if (open_) {
      mz_zip_reader_end(&archive_);
    }
  }

  ZipReader(ZipReader const&) = delete;
  ZipReader& operator=(ZipReader const&) = delete;

  mz_zip_archive& archive() { return archive_; }

 private:
  mz_zip_archive archive_{};
  bool open_ = false;
};

std::wstring ReadEntryPath(mz_zip_archive& archive, mz_uint index) {
  const mz_uint size =
      mz_zip_reader_get_filename(&archive, index, nullptr, 0);
  if (size == 0) {
    Fail(L"The zip directory is invalid.");
  }

  std::vector<char> name(size);
  if (mz_zip_reader_get_filename(&archive, index, name.data(), size) == 0) {
    Fail(L"The zip directory is invalid.");
  }
  return NormalizeEntryPath(DecodeEntryName(name.data()));
}

void ValidateArchive(mz_zip_archive& archive) {
  const mz_uint count = mz_zip_reader_get_num_files(&archive);
  if (count > kMaxEntries) {
    Fail(L"The zip contains too many files.");
  }

  uint64_t totalBytes = 0;

  for (mz_uint index = 0; index < count; ++index) {
    mz_zip_archive_file_stat stat{};
    if (!mz_zip_reader_file_stat(&archive, index, &stat)) {
      Fail(L"The zip directory is invalid.");
    }

    const std::wstring path = ReadEntryPath(archive, index);
    if (path.empty()) {
      if (!stat.m_is_directory) {
        Fail(L"The zip contains an unsafe file path.");
      }
      continue;
    }
    if (stat.m_is_directory) {
      continue;
    }
    if (stat.m_is_encrypted) {
      Fail(L"Encrypted zip files are not supported.");
    }
    if (!stat.m_is_supported) {
      Fail(L"The zip uses an unsupported compression method.");
    }
    if (stat.m_uncomp_size > kMaxExtensionBytes ||
        totalBytes + stat.m_uncomp_size > kMaxExtensionBytes) {
      Fail(L"The extension is too large to install.");
    }

    totalBytes += stat.m_uncomp_size;
  }
}

IAsyncOperation<StorageFolder> EnsureFolderAsync(StorageFolder root,
                                                 std::wstring const& path) {
  StorageFolder current = root;
  size_t start = 0;
  while (start < path.size()) {
    const size_t slash = path.find(L'/', start);
    const std::wstring segment =
        slash == std::wstring::npos ? path.substr(start)
                                    : path.substr(start, slash - start);
    if (!segment.empty()) {
      current = co_await current.CreateFolderAsync(
          hstring(segment), CreationCollisionOption::OpenIfExists);
    }
    if (slash == std::wstring::npos) {
      break;
    }
    start = slash + 1;
  }
  co_return current;
}

IAsyncOperation<StorageFolder> CreateDestinationAsync(std::wstring& id) {
  const StorageFolder localFolder = ApplicationData::Current().LocalFolder();
  const StorageFolder extensionsFolder = co_await localFolder.CreateFolderAsync(
      kExtensionsFolderName, CreationCollisionOption::OpenIfExists);

  StorageFolder destination{nullptr};
  for (int attempt = 0; attempt < 5 && !destination; ++attempt) {
    id = MakeShortId();
    try {
      destination = co_await extensionsFolder.CreateFolderAsync(
          hstring(id), CreationCollisionOption::FailIfExists);
    } catch (hresult_error const&) {
    }
  }
  if (!destination) {
    Fail(L"Could not create a folder for the extension.");
  }
  co_return destination;
}

IAsyncOperation<hstring> ReadTextFileAsync(StorageFile const& file) {
  const auto buffer = co_await FileIO::ReadBufferAsync(file);
  std::vector<uint8_t> bytes(buffer.Length());
  if (!bytes.empty()) {
    std::memcpy(bytes.data(), buffer.data(), bytes.size());
  }
  co_return hstring(DecodeText(bytes));
}

}  // namespace

Manager& Manager::Get() {
  static Manager manager;
  return manager;
}

void Manager::Store(Extension extension) {
  for (auto& existing : extensions_) {
    if (existing.id == extension.id) {
      existing = std::move(extension);
      return;
    }
  }
  extensions_.push_back(std::move(extension));
}

IAsyncAction Manager::ReadExtensionAsync(StorageFolder folder,
                                         Extension& extension) const {
  extension = {};
  extension.id = folder.Name().c_str();
  log::Write(L"extensions: reading " + extension.id);

  const IStorageItem scriptItem =
      co_await folder.TryGetItemAsync(hstring(kUserscriptFileName));
  const StorageFile scriptFile =
      scriptItem ? scriptItem.try_as<StorageFile>() : nullptr;
  if (scriptFile) {
    extension.hasUserscript = true;
    try {
      const hstring code = co_await ReadTextFileAsync(scriptFile);
      extension.script = code.c_str();
    } catch (hresult_error const& error) {
      log::Write(L"extensions: " + extension.id + L" script read failed " +
                 std::wstring(error.message()));
    }
    ParseUserscriptMatches(extension.script, extension.patterns);
    log::Write(L"extensions: " + extension.id + L" has " +
               std::to_wstring(extension.patterns.size()) + L" patterns");
  }

  const IStorageItem indexItem =
      co_await folder.TryGetItemAsync(hstring(kIndexFileName));
  extension.hasIndexHtml = indexItem && indexItem.try_as<StorageFile>();
  if (extension.hasIndexHtml) {
    log::Write(L"extensions: " + extension.id + L" has index.html");
  }
}

IAsyncAction Manager::LoadAllAsync() {
  std::vector<Extension> loaded;
  log::Write(L"extensions: loading all");

  try {
    const StorageFolder localFolder =
        ApplicationData::Current().LocalFolder();
    log::Write(L"extensions: opening extensions folder");
    const IStorageItem extensionsItem = co_await localFolder.TryGetItemAsync(
        hstring(kExtensionsFolderName));
    const StorageFolder extensionsFolder =
        extensionsItem ? extensionsItem.try_as<StorageFolder>() : nullptr;
    if (!extensionsFolder) {
      log::Write(L"extensions: no extensions folder");
      extensions_ = std::move(loaded);
      co_return;
    }
    log::Write(L"extensions: enumerating extension folders");

    for (StorageFolder const& folder :
         co_await extensionsFolder.GetFoldersAsync()) {
      try {
        Extension extension;
        co_await ReadExtensionAsync(folder, extension);
        if (extension.hasUserscript || extension.hasIndexHtml) {
          loaded.push_back(std::move(extension));
        } else {
          log::Write(L"extensions: " + extension.id +
                     L" has no userscript or index.html");
        }
      } catch (hresult_error const& error) {
        log::Write(L"extensions: read failed " +
                   std::wstring(error.message()));
      } catch (...) {
        log::Write(L"extensions: read failed");
      }
    }
  } catch (hresult_error const& error) {
    log::Write(L"extensions: folder unavailable " +
               std::wstring(error.message()));
  } catch (...) {
    log::Write(L"extensions: folder unavailable");
  }

  log::Write(L"extensions: loaded " + std::to_wstring(loaded.size()));
  extensions_ = std::move(loaded);
}

IAsyncAction Manager::LoadAsync(std::wstring const& id) {
  log::Write(L"extensions: loading " + id);
  try {
    const StorageFolder folder =
        ApplicationData::Current().LocalFolder();
    const IStorageItem extensionsItem =
        co_await folder.TryGetItemAsync(hstring(kExtensionsFolderName));
    const StorageFolder extensionsFolder =
        extensionsItem ? extensionsItem.try_as<StorageFolder>() : nullptr;
    if (!extensionsFolder) {
      log::Write(L"extensions: load failed, no extensions folder");
      co_return;
    }
    const IStorageItem extensionItem =
        co_await extensionsFolder.TryGetItemAsync(hstring(id));
    const StorageFolder extensionFolder =
        extensionItem ? extensionItem.try_as<StorageFolder>() : nullptr;
    if (!extensionFolder) {
      log::Write(L"extensions: load failed, missing " + id);
      co_return;
    }
    Extension extension;
    co_await ReadExtensionAsync(extensionFolder, extension);
    if (extension.hasUserscript || extension.hasIndexHtml) {
      Store(std::move(extension));
    } else {
      log::Write(L"extensions: " + extension.id +
                 L" has no userscript or index.html");
    }
  } catch (hresult_error const& error) {
    log::Write(L"extensions: load failed " + id + L" " +
               std::wstring(error.message()));
  } catch (...) {
    log::Write(L"extensions: load failed " + id);
  }
}

IAsyncAction Manager::RemoveAsync(std::wstring const& id) {
  log::Write(L"extensions: removing " + id);
  try {
    const StorageFolder localFolder =
        ApplicationData::Current().LocalFolder();
    const IStorageItem extensionsItem =
        co_await localFolder.TryGetItemAsync(hstring(kExtensionsFolderName));
    const StorageFolder extensionsFolder =
        extensionsItem ? extensionsItem.try_as<StorageFolder>() : nullptr;
    if (!extensionsFolder) {
      log::Write(L"extensions: remove skipped, no extensions folder");
    } else {
      const IStorageItem extensionItem =
          co_await extensionsFolder.TryGetItemAsync(hstring(id));
      const StorageFolder extensionFolder =
          extensionItem ? extensionItem.try_as<StorageFolder>() : nullptr;
      if (extensionFolder) {
        co_await extensionFolder.DeleteAsync(
            StorageDeleteOption::PermanentDelete);
      }
    }
  } catch (hresult_error const& error) {
    log::Write(L"extensions: delete failed " + id + L" " +
               std::wstring(error.message()));
  } catch (...) {
    log::Write(L"extensions: delete failed " + id);
  }

  extensions_.erase(
      std::remove_if(extensions_.begin(), extensions_.end(),
                     [&id](Extension const& extension) {
                       return extension.id == id;
                     }),
      extensions_.end());
}

std::wstring Manager::BuildInjectionScript(std::wstring const& url) const {
  std::wstring script;
  for (auto const& extension : extensions_) {
    bool matched = extension.patterns.empty();
    for (auto const& pattern : extension.patterns) {
      if (MatchesUrl(pattern, url)) {
        matched = true;
        break;
      }
    }
    if (!matched || extension.script.empty()) {
      continue;
    }

    script += L"try{(function(extensionId){\n";
    script +=
        L"if(window.__edgeMinusBooted&&window.__edgeMinusBooted[extensionId])"
        L"return;\n";
    script +=
        L"(window.__edgeMinusBooted=window.__edgeMinusBooted||{})[extensionId]"
        L"=true;\n";
    script += extension.script;
    if (extension.script.back() != L'\n') {
      script += L'\n';
    }
    script += L"\n})(";
    script += JsString(extension.id);
    script += L");}catch(e){}\n";
  }
  return script;
}

bool MatchesUrl(std::wstring const& pattern, std::wstring const& url) {
  const std::wstring trimmed = Trim(pattern);
  if (trimmed.empty() || url.empty()) {
    return false;
  }
  if (_wcsicmp(trimmed.c_str(), L"<all_urls>") == 0) {
    return true;
  }

  if (trimmed.find(L"://") == std::wstring::npos) {
    if (WildcardMatch(trimmed, url, true)) {
      return true;
    }
    return MatchesUrl(L"*://" + trimmed, url);
  }

  const UrlParts patternParts = SplitUrl(trimmed);
  const UrlParts urlParts = SplitUrl(url);

  if (patternParts.scheme == L"*") {
    if (_wcsicmp(urlParts.scheme.c_str(), L"http") != 0 &&
        _wcsicmp(urlParts.scheme.c_str(), L"https") != 0) {
      return false;
    }
  } else if (_wcsicmp(patternParts.scheme.c_str(),
                      urlParts.scheme.c_str()) != 0) {
    return false;
  }

  if (patternParts.host != L"*") {
    bool hostMatches =
        WildcardMatch(patternParts.host, urlParts.host, true);
    if (!hostMatches && patternParts.host.rfind(L"*.", 0) == 0) {
      hostMatches = _wcsicmp(patternParts.host.c_str() + 2,
                             urlParts.host.c_str()) == 0;
    }
    if (!hostMatches) {
      return false;
    }
  }

  const std::wstring pathPattern =
      patternParts.path.empty() ? L"/" : patternParts.path;
  const std::wstring path = urlParts.path.empty() ? L"/" : urlParts.path;
  return WildcardMatch(pathPattern, path, false);
}

namespace {

IAsyncOperation<hstring> InstallZipAsync(StorageFile const& zipFile) {
  log::Write(L"extension: reading zip");
  const auto buffer = co_await FileIO::ReadBufferAsync(zipFile);
  log::Write(L"extension: zip bytes " +
             std::to_wstring(buffer.Length()));
  if (buffer.Length() > kMaxZipBytes) {
    Fail(L"The zip file is too large.");
  }

  std::vector<uint8_t> bytes(buffer.Length());
  if (!bytes.empty()) {
    std::memcpy(bytes.data(), buffer.data(), bytes.size());
  }

  ZipReader reader(bytes);
  mz_zip_archive& archive = reader.archive();
  ValidateArchive(archive);
  log::Write(L"extension: zip validated");

  std::wstring id;
  const StorageFolder destination = co_await CreateDestinationAsync(id);
  log::Write(L"extension: destination " + id);

  std::exception_ptr failure;
  try {
    const mz_uint count = mz_zip_reader_get_num_files(&archive);
    log::Write(L"extension: extracting " + std::to_wstring(count) +
               L" entries");
    for (mz_uint index = 0; index < count; ++index) {
      mz_zip_archive_file_stat stat{};
      if (!mz_zip_reader_file_stat(&archive, index, &stat)) {
        Fail(L"The zip directory is invalid.");
      }

      const std::wstring path = ReadEntryPath(archive, index);
      if (path.empty()) {
        continue;
      }
      log::Write(L"extension: entry " + path);
      if (stat.m_is_directory) {
        co_await EnsureFolderAsync(destination, path);
        continue;
      }

      const size_t slash = path.find_last_of(L'/');
      const std::wstring parent = slash == std::wstring::npos
                                      ? std::wstring()
                                      : path.substr(0, slash);
      const std::wstring name = slash == std::wstring::npos
                                    ? path
                                    : path.substr(slash + 1);

      const StorageFolder folder =
          co_await EnsureFolderAsync(destination, parent);

      std::vector<uint8_t> data;
      if (stat.m_uncomp_size != 0) {
        data.resize(static_cast<size_t>(stat.m_uncomp_size));
        if (!mz_zip_reader_extract_to_mem(&archive, index, data.data(),
                                          data.size(), 0)) {
          Fail(L"The zip contains a corrupt file.");
        }
      }
      log::Write(L"extension: writing " + path);

      const StorageFile file = co_await folder.CreateFileAsync(
          hstring(name), CreationCollisionOption::ReplaceExisting);
      if (!data.empty()) {
        co_await FileIO::WriteBytesAsync(file, data);
      }
      log::Write(L"extension: wrote " + path);
    }
  } catch (...) {
    failure = std::current_exception();
  }

  if (failure) {
    log::Write(L"extension: install failed, cleaning up");
    co_await destination.DeleteAsync(StorageDeleteOption::PermanentDelete);
    std::rethrow_exception(failure);
  }

  log::Write(L"extension: install complete " + id);
  co_return hstring(id);
}

}  // namespace

IAsyncOperation<hstring> InstallAsync(StorageFile const& file) {
  const std::wstring fileName = file.Name().c_str();
  if (EndsWithIgnoreCase(fileName, L".zip")) {
    const hstring id = co_await InstallZipAsync(file);
    co_return id;
  }

  std::wstring targetName;
  if (EndsWithIgnoreCase(fileName, L".js")) {
    targetName = kUserscriptFileName;
  } else if (EndsWithIgnoreCase(fileName, L".html") ||
             EndsWithIgnoreCase(fileName, L".htm")) {
    targetName = kIndexFileName;
  } else {
    Fail(L"Only .zip, .js or .html extension files are supported.");
  }

  log::Write(L"extension: reading " + fileName);
  const auto buffer = co_await FileIO::ReadBufferAsync(file);
  if (buffer.Length() > kMaxExtensionBytes) {
    Fail(L"The extension file is too large.");
  }

  std::vector<uint8_t> bytes(buffer.Length());
  if (!bytes.empty()) {
    std::memcpy(bytes.data(), buffer.data(), bytes.size());
  }

  std::wstring id;
  const StorageFolder destination = co_await CreateDestinationAsync(id);
  log::Write(L"extension: destination " + id);

  std::exception_ptr failure;
  try {
    const StorageFile target = co_await destination.CreateFileAsync(
        hstring(targetName), CreationCollisionOption::ReplaceExisting);
    if (!bytes.empty()) {
      co_await FileIO::WriteBytesAsync(target, bytes);
    }
  } catch (...) {
    failure = std::current_exception();
  }

  if (failure) {
    log::Write(L"extension: install failed, cleaning up");
    co_await destination.DeleteAsync(StorageDeleteOption::PermanentDelete);
    std::rethrow_exception(failure);
  }

  log::Write(L"extension: install complete " + id);
  co_return hstring(id);
}

}  // namespace edgeminus::extensions
