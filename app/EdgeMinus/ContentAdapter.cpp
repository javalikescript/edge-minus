#include "pch.h"

#include "ContentAdapter.h"

#include <cstring>
#include <vector>

#include "Log.h"

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Storage::Streams;
using namespace winrt::Windows::Web;
using namespace winrt::Windows::Web::Http;

namespace edgeminus::content {
namespace {

constexpr wchar_t kSchemeSeparator[] = L"://";

bool IsSpace(wchar_t ch) {
  return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n' ||
         ch == L'\f';
}

bool IsNameChar(wchar_t ch) {
  return std::iswalnum(ch) || ch == L'-' || ch == L':' || ch == L'_' ||
         ch == L'.';
}

wchar_t LowerChar(wchar_t ch) {
  return static_cast<wchar_t>(std::towlower(ch));
}

std::wstring Lower(std::wstring value) {
  std::transform(value.begin(), value.end(), value.begin(), LowerChar);
  return value;
}

std::wstring Trim(std::wstring value) {
  const size_t first = value.find_first_not_of(L" \t\r\n");
  if (first == std::wstring::npos) {
    return {};
  }
  const size_t last = value.find_last_not_of(L" \t\r\n");
  return value.substr(first, last - first + 1);
}

bool StartsWithIgnoreCase(std::wstring const& text, std::wstring_view prefix) {
  return text.size() >= prefix.size() &&
         _wcsnicmp(text.c_str(), prefix.data(), prefix.size()) == 0;
}

size_t FindIgnoreCase(std::wstring const& text, std::wstring const& needle,
                      size_t start) {
  if (needle.empty() || needle.size() > text.size()) {
    return std::wstring::npos;
  }
  const size_t last = text.size() - needle.size();
  for (size_t index = start; index <= last; ++index) {
    bool match = true;
    for (size_t offset = 0; offset < needle.size(); ++offset) {
      if (LowerChar(text[index + offset]) != LowerChar(needle[offset])) {
        match = false;
        break;
      }
    }
    if (match) {
      return index;
    }
  }
  return std::wstring::npos;
}

std::wstring NormalizeQuery(std::wstring value) {
  if (!value.empty() && value.front() != L'?') {
    value.insert(value.begin(), L'?');
  }
  return value;
}

std::wstring NormalizeFragment(std::wstring value) {
  if (!value.empty() && value.front() != L'#') {
    value.insert(value.begin(), L'#');
  }
  return value;
}

std::wstring UriScheme(std::wstring const& url) {
  const size_t separator = url.find(kSchemeSeparator);
  if (separator == std::wstring::npos) {
    return {};
  }
  return Lower(url.substr(0, separator));
}

std::wstring TagName(std::wstring const& tag) {
  size_t index = 1;
  if (index < tag.size() && tag[index] == L'/') {
    ++index;
  }
  const size_t start = index;
  while (index < tag.size() && IsNameChar(tag[index])) {
    ++index;
  }
  return Lower(tag.substr(start, index - start));
}

size_t FindTagEnd(std::wstring const& html, size_t start) {
  wchar_t quote = 0;
  for (size_t index = start; index < html.size(); ++index) {
    const wchar_t ch = html[index];
    if (quote != 0) {
      if (ch == quote) {
        quote = 0;
      }
    } else if (ch == L'"' || ch == L'\'') {
      quote = ch;
    } else if (ch == L'>') {
      return index;
    }
  }
  return std::wstring::npos;
}

bool IsTargetTag(std::wstring const& name) {
  return name == L"a" || name == L"link" || name == L"area" ||
         name == L"base" || name == L"img" || name == L"script" ||
         name == L"iframe" || name == L"use";
}

bool IsTargetAttribute(std::wstring const& tag, std::wstring const& attribute) {
  if (attribute == L"href" || attribute == L"src" || attribute == L"action") {
    return true;
  }
  return tag == L"use" && attribute == L"xlink:href";
}

std::wstring AdaptReference(std::wstring const& value,
                            std::wstring const& remoteUrl, bool isBase) {
  if (value.empty()) {
    return {};
  }

  const bool absolute = StartsWithIgnoreCase(value, L"http://") ||
                        StartsWithIgnoreCase(value, L"https://");
  const bool protocolRelative = value.size() >= 2 && value[0] == L'/' &&
                                value[1] == L'/';
  const bool rootRelative = value.front() == L'/' && !protocolRelative;
  if (!absolute && !protocolRelative && !rootRelative && !isBase) {
    return {};
  }

  try {
    std::wstring full;
    if (absolute) {
      full = value;
    } else if (protocolRelative) {
      full = UriScheme(remoteUrl) + L":" + value;
    }

    std::wstring local;
    if (!full.empty()) {
      local = LocalPathFromUri(Uri(hstring(full)));
    } else {
      local = LocalPathFromUri(Uri(hstring(remoteUrl), hstring(value)));
    }
    if (local.empty()) {
      return {};
    }
    return L"/" + local;
  } catch (...) {
    return {};
  }
}

std::wstring RewriteTag(std::wstring const& tag, std::wstring const& tagName,
                        std::wstring const& remoteUrl) {
  struct Replacement {
    size_t start;
    size_t length;
    std::wstring text;
  };
  std::vector<Replacement> replacements;

  size_t index = 1;
  while (index < tag.size() && IsNameChar(tag[index])) {
    ++index;
  }

  while (index < tag.size()) {
    while (index < tag.size() && IsSpace(tag[index])) {
      ++index;
    }
    if (index >= tag.size() || tag[index] == L'>') {
      break;
    }
    if (tag[index] == L'/' && index + 1 < tag.size() &&
        tag[index + 1] == L'>') {
      break;
    }

    const size_t nameStart = index;
    while (index < tag.size() && !IsSpace(tag[index]) && tag[index] != L'=' &&
           tag[index] != L'>' && tag[index] != L'/') {
      ++index;
    }
    if (index == nameStart) {
      ++index;
      continue;
    }
    const std::wstring attribute =
        Lower(tag.substr(nameStart, index - nameStart));

    while (index < tag.size() && IsSpace(tag[index])) {
      ++index;
    }
    if (index >= tag.size() || tag[index] != L'=') {
      continue;
    }
    ++index;
    while (index < tag.size() && IsSpace(tag[index])) {
      ++index;
    }
    if (index >= tag.size()) {
      break;
    }

    wchar_t quote = 0;
    size_t replacementStart = index;
    size_t valueStart = index;
    size_t valueEnd = index;
    size_t replacementEnd = index;

    if (tag[index] == L'"' || tag[index] == L'\'') {
      quote = tag[index];
      valueStart = index + 1;
      valueEnd = tag.find(quote, valueStart);
      if (valueEnd == std::wstring::npos) {
        valueEnd = tag.size();
        replacementEnd = tag.size();
      } else {
        replacementEnd = valueEnd + 1;
      }
    } else {
      while (index < tag.size() && !IsSpace(tag[index]) &&
             tag[index] != L'>') {
        ++index;
      }
      valueEnd = index;
      replacementEnd = index;
    }
    index = replacementEnd;

    if (!IsTargetAttribute(tagName, attribute)) {
      continue;
    }
    const std::wstring value = tag.substr(valueStart, valueEnd - valueStart);
    const std::wstring adapted =
        AdaptReference(value, remoteUrl, tagName == L"base");
    if (adapted.empty() || adapted == value) {
      continue;
    }

    std::wstring text;
    if (quote != 0) {
      text.push_back(quote);
      text += adapted;
      text.push_back(quote);
    } else {
      text = adapted;
    }
    replacements.push_back({replacementStart, replacementEnd - replacementStart,
                            std::move(text)});
  }

  std::wstring result = tag;
  for (auto it = replacements.rbegin(); it != replacements.rend(); ++it) {
    result.replace(it->start, it->length, it->text);
  }
  return result;
}

uint32_t CodePageFromCharset(std::wstring const& charset) {
  const std::wstring value = Lower(Trim(charset));
  if (value.empty()) {
    return 0;
  }
  if (value == L"utf-8" || value == L"utf8" || value == L"unicode-1-1-utf-8") {
    return CP_UTF8;
  }
  if (value == L"us-ascii" || value == L"ascii") {
    return 20127;
  }
  if (value == L"iso-8859-1" || value == L"latin1" || value == L"latin-1" ||
      value == L"windows-1252" || value == L"cp1252") {
    return 1252;
  }
  if (value == L"windows-1251" || value == L"cp1251") {
    return 1251;
  }
  if (value == L"shift_jis" || value == L"shift-jis" || value == L"sjis") {
    return 932;
  }
  if (value == L"euc-jp") {
    return 51932;
  }
  if (value == L"gb2312" || value == L"gbk" || value == L"gb18030") {
    return 936;
  }
  if (value == L"big5" || value == L"big-5") {
    return 950;
  }
  if (value == L"euc-kr" || value == L"ks_c_5601-1987") {
    return 949;
  }
  if (value == L"utf-16" || value == L"utf-16le" || value == L"unicode") {
    return 1200;
  }
  if (value == L"utf-16be") {
    return 1201;
  }
  return 0;
}

std::wstring DecodeWith(uint32_t codePage, DWORD flags, const uint8_t* data,
                        size_t length) {
  if (length == 0) {
    return {};
  }
  const char* text = reinterpret_cast<const char*>(data);
  const int byteCount = static_cast<int>(length);
  int needed = MultiByteToWideChar(codePage, flags, text, byteCount, nullptr, 0);
  if (needed <= 0) {
    return {};
  }
  std::wstring result(static_cast<size_t>(needed), L'\0');
  MultiByteToWideChar(codePage, flags, text, byteCount, result.data(), needed);
  return result;
}

std::wstring DecodeHtml(std::vector<uint8_t> const& bytes,
                        std::wstring const& charset) {
  const size_t length = bytes.size();
  if (length == 0) {
    return {};
  }
  const uint8_t* data = bytes.data();

  if (length >= 2 && data[0] == 0xFF && data[1] == 0xFE) {
    const size_t units = (length - 2) / 2;
    std::wstring result(units, L'\0');
    if (units != 0) {
      std::memcpy(result.data(), data + 2, units * 2);
    }
    return result;
  }
  if (length >= 2 && data[0] == 0xFE && data[1] == 0xFF) {
    const size_t units = (length - 2) / 2;
    std::wstring result(units, L'\0');
    for (size_t index = 0; index < units; ++index) {
      result[index] = static_cast<wchar_t>((data[2 + index * 2] << 8) |
                                           data[3 + index * 2]);
    }
    return result;
  }

  size_t offset = 0;
  if (length >= 3 && data[0] == 0xEF && data[1] == 0xBB &&
      data[2] == 0xBF) {
    offset = 3;
  }

  const uint32_t codePage = CodePageFromCharset(charset);
  if (codePage != 0) {
    const std::wstring decoded =
        DecodeWith(codePage, codePage == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0,
                   data + offset, length - offset);
    if (!decoded.empty()) {
      return decoded;
    }
    if (codePage == CP_UTF8) {
      const std::wstring fallback = DecodeWith(CP_ACP, 0, data + offset,
                                               length - offset);
      if (!fallback.empty() || length == offset) {
        return fallback;
      }
    }
  }

  const std::wstring utf8 =
      DecodeWith(CP_UTF8, MB_ERR_INVALID_CHARS, data + offset, length - offset);
  if (!utf8.empty() || length == offset) {
    return utf8;
  }
  return DecodeWith(CP_ACP, 0, data + offset, length - offset);
}

std::vector<uint8_t> EncodeUtf8(std::wstring const& text, bool withBom) {
  std::vector<uint8_t> bytes;
  if (withBom) {
    bytes.push_back(0xEF);
    bytes.push_back(0xBB);
    bytes.push_back(0xBF);
  }
  if (text.empty()) {
    return bytes;
  }
  const int wideLength = static_cast<int>(text.size());
  const int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), wideLength,
                                         nullptr, 0, nullptr, nullptr);
  if (needed <= 0) {
    return bytes;
  }
  const size_t offset = bytes.size();
  bytes.resize(offset + static_cast<size_t>(needed));
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(), wideLength,
                      reinterpret_cast<char*>(bytes.data() + offset), needed,
                      nullptr, nullptr);
  return bytes;
}

}  // namespace

std::wstring LocalPathFromUri(Uri const& uri) {
  const std::wstring scheme = Lower(std::wstring(uri.SchemeName()));
  if (scheme != L"http" && scheme != L"https") {
    return {};
  }

  const std::wstring host = std::wstring(uri.Host());
  if (host.empty()) {
    return {};
  }

  int port = uri.Port();
  if (port <= 0 || (scheme == L"http" && port == 80) ||
      (scheme == L"https" && port == 443)) {
    port = 0;
  }

  std::wstring path = std::wstring(uri.Path());
  if (path.empty()) {
    path = L"/";
  }

  std::wstring result =
      scheme + L"-" + host + L"-" + std::to_wstring(port) + path;
  result += NormalizeQuery(std::wstring(uri.Query()));
  result += NormalizeFragment(std::wstring(uri.Fragment()));
  return result;
}

std::wstring RemoteUriFromLocalPath(std::wstring const& localPath) {
  std::wstring value = localPath;
  size_t start = 0;
  while (start < value.size() && value[start] == L'/') {
    ++start;
  }
  value = value.substr(start);

  const size_t suffixStart = value.find_first_of(L"?#");
  const std::wstring encoded = value.substr(0, suffixStart);
  const std::wstring suffix =
      suffixStart == std::wstring::npos ? std::wstring()
                                        : value.substr(suffixStart);

  const size_t slash = encoded.find(L'/');
  const std::wstring authority =
      slash == std::wstring::npos ? encoded : encoded.substr(0, slash);
  const std::wstring path =
      slash == std::wstring::npos ? L"/" : encoded.substr(slash);

  const size_t schemeEnd = authority.find(L'-');
  const size_t portStart = authority.rfind(L'-');
  if (schemeEnd == std::wstring::npos || portStart == std::wstring::npos ||
      portStart <= schemeEnd) {
    return {};
  }

  const std::wstring scheme = Lower(authority.substr(0, schemeEnd));
  const std::wstring host =
      authority.substr(schemeEnd + 1, portStart - schemeEnd - 1);
  const std::wstring portText = authority.substr(portStart + 1);
  if ((scheme != L"http" && scheme != L"https") || host.empty()) {
    return {};
  }

  int port = 0;
  for (wchar_t ch : portText) {
    if (!std::iswdigit(ch)) {
      return {};
    }
    port = port * 10 + (ch - L'0');
  }

  std::wstring result = scheme + L"://" + host;
  if (port > 0) {
    result += L":" + std::to_wstring(port);
  }
  result += path;
  result += suffix;
  return result;
}

std::wstring AdaptHtml(std::wstring const& html,
                       std::wstring const& remoteUrl) {
  std::wstring output;
  output.reserve(html.size() + 64);

  size_t index = 0;
  const size_t length = html.size();
  while (index < length) {
    const wchar_t ch = html[index];
    if (ch != L'<') {
      output.push_back(ch);
      ++index;
      continue;
    }

    if (index + 4 <= length && html.compare(index, 4, L"<!--") == 0) {
      const size_t end = html.find(L"-->", index + 4);
      if (end == std::wstring::npos) {
        output.append(html, index, length - index);
        break;
      }
      output.append(html, index, end + 3 - index);
      index = end + 3;
      continue;
    }

    if (index + 1 < length &&
        (html[index + 1] == L'!' || html[index + 1] == L'?')) {
      const size_t end = html.find(L'>', index + 1);
      if (end == std::wstring::npos) {
        output.append(html, index, length - index);
        break;
      }
      output.append(html, index, end + 1 - index);
      index = end + 1;
      continue;
    }

    if (index + 1 < length && html[index + 1] == L'/') {
      const size_t end = FindTagEnd(html, index + 2);
      if (end == std::wstring::npos) {
        output.append(html, index, length - index);
        break;
      }
      output.append(html, index, end + 1 - index);
      index = end + 1;
      continue;
    }

    if (index + 1 >= length || !IsNameChar(html[index + 1])) {
      output.push_back(ch);
      ++index;
      continue;
    }

    const size_t end = FindTagEnd(html, index + 1);
    if (end == std::wstring::npos) {
      output.append(html, index, length - index);
      break;
    }

    const std::wstring tag = html.substr(index, end + 1 - index);
    const std::wstring name = TagName(tag);
    if (IsTargetTag(name)) {
      output += RewriteTag(tag, name, remoteUrl);
    } else {
      output += tag;
    }
    index = end + 1;

    if (name == L"script" || name == L"style") {
      const size_t closeStart =
          FindIgnoreCase(html, L"</" + name, index);
      if (closeStart == std::wstring::npos) {
        output.append(html, index, length - index);
        break;
      }
      const size_t closeEnd = FindTagEnd(html, closeStart + 2);
      if (closeEnd == std::wstring::npos) {
        output.append(html, index, length - index);
        break;
      }
      output.append(html, index, closeEnd + 1 - index);
      index = closeEnd + 1;
    }
  }
  return output;
}

StreamResolver::StreamResolver() {
  client_ = HttpClient();
  client_.DefaultRequestHeaders().TryAppendWithoutValidation(
      L"Accept",
      L"text/html,application/xhtml+xml,application/xml;q=0.9,"
      L"*/*;q=0.8");
}

IAsyncOperation<IInputStream> StreamResolver::UriToStreamAsync(
    Uri const& uri) {
  auto lifetime = get_strong();
  if (!uri) {
    throw hresult_error(E_INVALIDARG, L"Missing local stream URI.");
  }

  std::wstring localPath = std::wstring(uri.Path());
  localPath += NormalizeQuery(std::wstring(uri.Query()));
  localPath += NormalizeFragment(std::wstring(uri.Fragment()));

  const std::wstring remoteUrl = RemoteUriFromLocalPath(localPath);
  if (remoteUrl.empty()) {
    log::Write(L"content: unsupported local uri " + localPath);
    throw hresult_error(E_INVALIDARG, L"Unsupported local stream URI.");
  }

  log::Write(L"content: fetch " + remoteUrl);

  HttpRequestMessage request(HttpMethod::Get(), Uri(hstring(remoteUrl)));
  const auto response = co_await client_.SendRequestAsync(request);

  std::wstring mediaType;
  std::wstring charset;
  try {
    const auto contentType = response.Content().Headers().ContentType();
    mediaType = Lower(std::wstring(contentType.MediaType()));
    charset = std::wstring(contentType.CharSet());
  } catch (...) {
  }

  if (mediaType != L"text/html") {
    co_return co_await response.Content().ReadAsInputStreamAsync();
  }

  const auto buffer = co_await response.Content().ReadAsBufferAsync();
  std::vector<uint8_t> bytes(buffer.Length());
  if (!bytes.empty()) {
    std::memcpy(bytes.data(), buffer.data(), bytes.size());
  }

  std::wstring documentUrl = remoteUrl;
  try {
    documentUrl =
        std::wstring(response.RequestMessage().RequestUri().AbsoluteUri());
  } catch (...) {
  }

  const std::wstring html = DecodeHtml(bytes, charset);
  const std::wstring adapted = AdaptHtml(html, documentUrl);
  const std::vector<uint8_t> encoded = EncodeUtf8(adapted, true);

  InMemoryRandomAccessStream memory;
  DataWriter writer(memory);
  writer.WriteBytes(encoded);
  co_await writer.StoreAsync();
  writer.DetachStream();
  memory.Seek(0);
  co_return memory.GetInputStreamAt(0);
}

}  // namespace edgeminus::content
