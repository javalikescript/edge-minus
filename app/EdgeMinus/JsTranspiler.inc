#include "JsTranspiler.h"

#include <algorithm>
#include <cwctype>
#include <vector>

namespace edgeminus::content {
namespace {

using std::wstring;
using std::vector;

constexpr wchar_t kOcHelper[] = L"__em_oc";
constexpr wchar_t kNullishHelper[] = L"__em_nc";
constexpr wchar_t kSpreadHelper[] = L"__em_spread";
constexpr wchar_t kRestHelper[] = L"__em_rest";
constexpr wchar_t kPrivateInHelper[] = L"__em_private_in";

struct Usage {
  bool optionalChain{false};
  bool nullish{false};
  bool spread{false};
  bool rest{false};
  bool privateIn{false};
  int counter{0};
};

wstring TranspileInner(wstring const& source, Usage& usage);

enum class TK { Ident, Number, String, Template, Regex, Punct };

struct Tok {
  TK kind;
  size_t begin;
  size_t end;
  bool nl;
};

wstring Text(wstring const& src, Tok const& tok) {
  return src.substr(tok.begin, tok.end - tok.begin);
}

bool IsPunct(wstring const& src, Tok const& tok, wchar_t const* value) {
  if (tok.kind != TK::Punct) {
    return false;
  }
  const size_t length = wcslen(value);
  return tok.end - tok.begin == length &&
         src.compare(tok.begin, length, value) == 0;
}

bool IsIdent(wstring const& src, Tok const& tok, wchar_t const* value) {
  if (tok.kind != TK::Ident) {
    return false;
  }
  const size_t length = wcslen(value);
  return tok.end - tok.begin == length &&
         src.compare(tok.begin, length, value) == 0;
}

bool IsIdentifierText(wstring const& src, Tok const& tok) {
  return tok.kind == TK::Ident && !Text(src, tok).empty() &&
         Text(src, tok)[0] != L'#';
}

bool IsPrivateName(wstring const& src, Tok const& tok) {
  return tok.kind == TK::Ident && tok.end > tok.begin &&
         src[tok.begin] == L'#';
}

bool IsOpen(wstring const& src, Tok const& tok) {
  return IsPunct(src, tok, L"(") || IsPunct(src, tok, L"[") ||
         IsPunct(src, tok, L"{");
}

bool IsClose(wstring const& src, Tok const& tok) {
  return IsPunct(src, tok, L")") || IsPunct(src, tok, L"]") ||
         IsPunct(src, tok, L"}");
}

bool IsIdStart(wchar_t ch) {
  return std::iswalpha(ch) || ch == L'_' || ch == L'$' || ch > 0x7F;
}

bool IsIdPart(wchar_t ch) {
  return std::iswalnum(ch) || ch == L'_' || ch == L'$' || ch > 0x7F;
}

bool IsRegexKeyword(wstring const& word) {
  static const wchar_t* keywords[] = {
      L"return", L"throw", L"case",     L"delete", L"void",   L"typeof",
      L"new",    L"in",    L"instanceof", L"do",   L"else",   L"yield",
      L"await",  L"of"};
  for (auto keyword : keywords) {
    if (word == keyword) {
      return true;
    }
  }
  return false;
}

bool RegexAllowed(wstring const& src, const Tok* previous) {
  if (previous == nullptr) {
    return true;
  }
  if (previous->kind == TK::Ident) {
    return IsRegexKeyword(Text(src, *previous));
  }
  if (previous->kind == TK::Punct) {
    return !(IsPunct(src, *previous, L")") || IsPunct(src, *previous, L"]") ||
             IsPunct(src, *previous, L"++") ||
             IsPunct(src, *previous, L"--"));
  }
  return false;
}

size_t ScanString(wstring const& src, size_t index) {
  const wchar_t quote = src[index];
  ++index;
  while (index < src.size()) {
    const wchar_t ch = src[index];
    if (ch == L'\\') {
      index += 2;
    } else if (ch == quote) {
      return index + 1;
    } else {
      ++index;
    }
  }
  return src.size();
}

size_t ScanRegex(wstring const& src, size_t index) {
  ++index;
  bool inClass = false;
  while (index < src.size()) {
    const wchar_t ch = src[index];
    if (ch == L'\\') {
      index += 2;
    } else if (ch == L'[') {
      inClass = true;
      ++index;
    } else if (ch == L']') {
      inClass = false;
      ++index;
    } else if (ch == L'/' && !inClass) {
      ++index;
      while (index < src.size() && IsIdPart(src[index])) {
        ++index;
      }
      return index;
    } else {
      ++index;
    }
  }
  return src.size();
}

size_t ScanTemplateExpression(wstring const& src, size_t index);

size_t ScanTemplate(wstring const& src, size_t index) {
  ++index;
  while (index < src.size()) {
    const wchar_t ch = src[index];
    if (ch == L'\\') {
      index += 2;
    } else if (ch == L'`') {
      return index + 1;
    } else if (ch == L'$' && index + 1 < src.size() && src[index + 1] == L'{') {
      index = ScanTemplateExpression(src, index + 2);
    } else {
      ++index;
    }
  }
  return src.size();
}

size_t ScanTemplateExpression(wstring const& src, size_t index) {
  int depth = 1;
  wchar_t previous = 0;
  while (index < src.size()) {
    const wchar_t ch = src[index];
    if (ch == L'\'' || ch == L'"') {
      index = ScanString(src, index);
      previous = L'x';
    } else if (ch == L'`') {
      index = ScanTemplate(src, index);
      previous = L'x';
    } else if (ch == L'/' && index + 1 < src.size() && src[index + 1] == L'/') {
      index = src.find(L'\n', index);
      if (index == wstring::npos) {
        return src.size();
      }
    } else if (ch == L'/' && index + 1 < src.size() && src[index + 1] == L'*') {
      index = src.find(L"*/", index + 2);
      index = index == wstring::npos ? src.size() : index + 2;
    } else if (ch == L'/' && !IsIdPart(previous) && previous != L')' &&
               previous != L']' && previous != L'}') {
      index = ScanRegex(src, index);
      previous = L'x';
    } else if (ch == L'{') {
      ++depth;
      ++index;
      previous = ch;
    } else if (ch == L'}') {
      --depth;
      ++index;
      if (depth == 0) {
        return index;
      }
      previous = ch;
    } else {
      if (!std::iswspace(ch)) {
        previous = ch;
      }
      ++index;
    }
  }
  return src.size();
}

vector<Tok> Tokenize(wstring const& src) {
  vector<Tok> tokens;
  size_t index = 0;
  bool newline = false;
  const Tok* previous = nullptr;
  static const wchar_t* puncts[] = {
      L">>>=", L"===",  L"!==",  L"**=", L"<<=", L">>=", L"&&=", L"||=",
      L"??=",  L"...",  L">>>",  L"=>",  L"==",  L"!=",  L"<=",  L">=",
      L"++",   L"--",   L"+=",   L"-=",  L"*=",  L"/=",  L"%=",  L"&=",
      L"|=",   L"^=",   L"**",   L"<<",  L">>",  L"&&",  L"||",  L"??",
      L"?.",   L"{",    L"}",    L"(",   L")",   L"[",   L"]",   L";",
      L",",    L"<",    L">",    L"+",   L"-",   L"*",   L"/",   L"%",
      L"&",    L"|",    L"^",    L"!",   L"~",   L"?",   L":",   L"=",
      L".",    L"@"};

  while (index < src.size()) {
    const wchar_t ch = src[index];
    if (std::iswspace(ch)) {
      if (ch == L'\n' || ch == L'\r' || ch == L'\f' || ch == 0x2028 ||
          ch == 0x2029) {
        newline = true;
      }
      ++index;
      continue;
    }
    if (ch == L'/' && index + 1 < src.size() && src[index + 1] == L'/') {
      const size_t end = src.find(L'\n', index);
      index = end == wstring::npos ? src.size() : end;
      continue;
    }
    if (ch == L'/' && index + 1 < src.size() && src[index + 1] == L'*') {
      const size_t end = src.find(L"*/", index + 2);
      size_t stop = end == wstring::npos ? src.size() : end + 2;
      if (src.find(L'\n', index) < stop) {
        newline = true;
      }
      index = stop;
      continue;
    }

    Tok token{};
    token.begin = index;
    token.nl = newline;
    if (ch == L'\'' || ch == L'"') {
      token.kind = TK::String;
      index = ScanString(src, index);
    } else if (ch == L'`') {
      token.kind = TK::Template;
      index = ScanTemplate(src, index);
    } else if (ch == L'#' && index + 1 < src.size() &&
               IsIdStart(src[index + 1])) {
      token.kind = TK::Ident;
      index += 2;
      while (index < src.size() && IsIdPart(src[index])) {
        ++index;
      }
    } else if (IsIdStart(ch)) {
      token.kind = TK::Ident;
      ++index;
      while (index < src.size() && IsIdPart(src[index])) {
        ++index;
      }
    } else if (std::iswdigit(ch) ||
               (ch == L'.' && index + 1 < src.size() &&
                std::iswdigit(src[index + 1]))) {
      token.kind = TK::Number;
      if (ch == L'0' && index + 1 < src.size() &&
          (src[index + 1] == L'x' || src[index + 1] == L'X' ||
           src[index + 1] == L'b' || src[index + 1] == L'B' ||
           src[index + 1] == L'o' || src[index + 1] == L'O')) {
        index += 2;
        while (index < src.size() &&
               (std::iswalnum(src[index]) || src[index] == L'_')) {
          ++index;
        }
      } else {
        while (index < src.size() &&
               (std::iswdigit(src[index]) || src[index] == L'_')) {
          ++index;
        }
        if (index < src.size() && src[index] == L'.') {
          ++index;
          while (index < src.size() &&
                 (std::iswdigit(src[index]) || src[index] == L'_')) {
            ++index;
          }
        }
        if (index < src.size() &&
            (src[index] == L'e' || src[index] == L'E')) {
          ++index;
          if (index < src.size() &&
              (src[index] == L'+' || src[index] == L'-')) {
            ++index;
          }
          while (index < src.size() && std::iswdigit(src[index])) {
            ++index;
          }
        }
        if (index < src.size() && src[index] == L'n') {
          ++index;
        }
      }
    } else if (ch == L'/' && RegexAllowed(src, previous)) {
      token.kind = TK::Regex;
      index = ScanRegex(src, index);
    } else {
      token.kind = TK::Punct;
      size_t length = 0;
      for (auto punct : puncts) {
        const size_t candidate = wcslen(punct);
        if (src.compare(index, candidate, punct) == 0) {
          if (candidate == 2 && punct[0] == L'?' && punct[1] == L'.' &&
              index + 2 < src.size() && std::iswdigit(src[index + 2])) {
            continue;
          }
          length = candidate;
          break;
        }
      }
      index += length == 0 ? 1 : length;
    }
    token.end = index;
    tokens.push_back(token);
    previous = &tokens.back();
    newline = false;
  }
  return tokens;
}

struct Edit {
  size_t begin;
  size_t end;
  wstring text;
};

wstring ApplyEdits(wstring const& src, vector<Edit> const& edits) {
  if (edits.empty()) {
    return src;
  }
  vector<Edit> sorted = edits;
  std::sort(sorted.begin(), sorted.end(),
            [](Edit const& left, Edit const& right) {
              return left.begin < right.begin;
            });
  wstring output;
  output.reserve(src.size() + 64);
  size_t position = 0;
  for (auto const& edit : sorted) {
    if (edit.begin < position) {
      continue;
    }
    output.append(src, position, edit.begin - position);
    output.append(edit.text);
    position = edit.end;
  }
  output.append(src, position, wstring::npos);
  return output;
}

size_t FindMatchingBackward(wstring const& src, vector<Tok> const& tokens,
                           size_t closeIndex) {
  int depth = 0;
  for (size_t index = closeIndex + 1; index-- > 0;) {
    if (IsClose(src, tokens[index])) {
      ++depth;
    } else if (IsOpen(src, tokens[index])) {
      if (depth == 0) {
        return index;
      }
      --depth;
    }
    if (index == 0) {
      break;
    }
  }
  return closeIndex;
}

size_t FindMatchingForward(wstring const& src, vector<Tok> const& tokens,
                           size_t openIndex) {
  int depth = 0;
  for (size_t index = openIndex; index < tokens.size(); ++index) {
    if (IsOpen(src, tokens[index])) {
      ++depth;
    } else if (IsClose(src, tokens[index])) {
      --depth;
      if (depth == 0) {
        return index;
      }
    }
  }
  return tokens.empty() ? 0 : tokens.size() - 1;
}

bool EndsExpression(wstring const& src, Tok const& token) {
  if (token.kind == TK::Ident || token.kind == TK::Number ||
      token.kind == TK::String || token.kind == TK::Template ||
      token.kind == TK::Regex) {
    return true;
  }
  if (token.kind == TK::Punct) {
    return IsPunct(src, token, L")") || IsPunct(src, token, L"]") ||
           IsPunct(src, token, L"}") || IsPunct(src, token, L"++") ||
           IsPunct(src, token, L"--");
  }
  return false;
}

bool IsLeftStopKeyword(wstring const& word) {
  static const wchar_t* keywords[] = {
      L"return", L"throw",  L"case",   L"default", L"else",   L"do",
      L"while",  L"for",    L"if",     L"switch",  L"try",    L"catch",
      L"finally", L"with",  L"function", L"class", L"const",  L"let",
      L"var",    L"export", L"import", L"break",   L"continue", L"debugger",
      L"yield",  L"of",     L"extends", L"static"};
  for (auto keyword : keywords) {
    if (word == keyword) {
      return true;
    }
  }
  return false;
}

bool IsLeftStopPunct(wstring const& src, Tok const& token) {
  static const wchar_t* puncts[] = {
      L";",  L",",  L"=",  L"+=", L"-=", L"*=", L"/=", L"%=", L"**=",
      L"<<=", L">>=", L">>>=", L"&=", L"|=", L"^=", L"&&=", L"||=",
      L"??=", L"=>", L":",  L"?",  L"||", L"&&", L"??", L"..."};
  for (auto punct : puncts) {
    if (IsPunct(src, token, punct)) {
      return true;
    }
  }
  return false;
}

size_t FindLeftStart(wstring const& src, vector<Tok> const& tokens,
                     size_t opIndex) {
  int depth = 0;
  size_t start = opIndex;
  for (size_t index = opIndex; index-- > 0;) {
    Tok const& token = tokens[index];
    if (IsPunct(src, token, L"}") && depth == 0) {
      break;
    }
    if (IsClose(src, token)) {
      ++depth;
      start = index;
      continue;
    }
    if (IsOpen(src, token)) {
      if (depth > 0) {
        --depth;
        start = index;
        continue;
      }
      break;
    }
    if (depth > 0) {
      start = index;
      continue;
    }
    if (token.kind == TK::Punct && IsLeftStopPunct(src, token)) {
      break;
    }
    if (token.kind == TK::Ident && IsLeftStopKeyword(Text(src, token))) {
      break;
    }
    start = index;
  }
  return start;
}

bool ContinuesExpression(wstring const& src, Tok const& token) {
  if (token.kind == TK::Punct) {
    static const wchar_t* puncts[] = {
        L".",   L"?.",  L"(",   L"[",   L"+",   L"-",   L"*",   L"/",
        L"%",   L"**",  L"<",   L">",   L"<=",  L">=",  L"==",  L"!=",
        L"===", L"!==", L"&",   L"|",   L"^",   L"&&",  L"||",  L"??",
        L"?",   L":",   L",",   L"=",   L"+=",  L"-=",  L"*=",  L"/=",
        L"%=",  L"**=", L"<<=", L">>=", L">>>=", L"&=", L"|=",  L"^=",
        L"&&=", L"||=", L"??=", L"<<"};
    for (auto punct : puncts) {
      if (IsPunct(src, token, punct)) {
        return true;
      }
    }
    return false;
  }
  if (token.kind == TK::Ident) {
    const wstring word = Text(src, token);
    return word == L"in" || word == L"instanceof" || word == L"of";
  }
  return token.kind == TK::Template;
}

size_t FindRightEnd(wstring const& src, vector<Tok> const& tokens,
                    size_t opIndex) {
  int depth = 0;
  for (size_t index = opIndex + 1; index < tokens.size(); ++index) {
    Tok const& token = tokens[index];
    if (IsOpen(src, token)) {
      ++depth;
      continue;
    }
    if (IsClose(src, token)) {
      if (depth > 0) {
        --depth;
        continue;
      }
      return index;
    }
    if (depth > 0) {
      continue;
    }
    if (token.kind == TK::Punct &&
        (IsPunct(src, token, L",") || IsPunct(src, token, L";") ||
         IsPunct(src, token, L":") || IsPunct(src, token, L"?") ||
         IsPunct(src, token, L"??") || IsPunct(src, token, L"||") ||
         IsPunct(src, token, L"&&") || IsPunct(src, token, L"=") ||
         IsPunct(src, token, L"+=") || IsPunct(src, token, L"-=") ||
         IsPunct(src, token, L"*=") || IsPunct(src, token, L"/=") ||
         IsPunct(src, token, L"%=") || IsPunct(src, token, L"**=") ||
         IsPunct(src, token, L"<<=") || IsPunct(src, token, L">>=") ||
         IsPunct(src, token, L">>>=") || IsPunct(src, token, L"&=") ||
         IsPunct(src, token, L"|=") || IsPunct(src, token, L"^=") ||
         IsPunct(src, token, L"&&=") || IsPunct(src, token, L"||=") ||
         IsPunct(src, token, L"??=") || IsPunct(src, token, L"=>"))) {
      return index;
    }
    if (index > opIndex + 1 && token.nl && tokens[index - 1].kind != TK::Punct &&
        tokens[index - 1].kind != TK::Template &&
        !ContinuesExpression(src, token)) {
      return index;
    }
  }
  return tokens.size();
}

size_t FindAssignRightEnd(wstring const& src, vector<Tok> const& tokens,
                          size_t opIndex) {
  int depth = 0;
  for (size_t index = opIndex + 1; index < tokens.size(); ++index) {
    Tok const& token = tokens[index];
    if (IsOpen(src, token)) {
      ++depth;
      continue;
    }
    if (IsClose(src, token)) {
      if (depth > 0) {
        --depth;
        continue;
      }
      return index;
    }
    if (depth > 0) {
      continue;
    }
    if (IsPunct(src, token, L",") || IsPunct(src, token, L";")) {
      return index;
    }
    if (index > opIndex + 1 && token.nl &&
        tokens[index - 1].kind != TK::Punct &&
        tokens[index - 1].kind != TK::Template &&
        !ContinuesExpression(src, token)) {
      return index;
    }
  }
  return tokens.size();
}

size_t FindPostfixStart(wstring const& src, vector<Tok> const& tokens,
                        size_t endExclusive) {
  size_t index = endExclusive;
  while (index > 0) {
    const size_t last = index - 1;
    Tok const& token = tokens[last];
    if (IsClose(src, token) && !IsPunct(src, token, L"}")) {
      const size_t open = FindMatchingBackward(src, tokens, last);
      if (open > 0 && EndsExpression(src, tokens[open - 1])) {
        index = open;
        continue;
      }
      return open;
    }
    if (IsPunct(src, token, L"}")) {
      return FindMatchingBackward(src, tokens, last);
    }
    if (token.kind == TK::Ident || token.kind == TK::Number ||
        token.kind == TK::String || token.kind == TK::Template ||
        token.kind == TK::Regex) {
      if (last > 0 && IsPunct(src, tokens[last - 1], L".")) {
        index = last - 1;
        continue;
      }
      return last;
    }
    if (IsPunct(src, token, L"++") || IsPunct(src, token, L"--")) {
      index = last;
      continue;
    }
    break;
  }
  return index;
}

bool ContainsAwaitOrYield(wstring const& src, vector<Tok> const& tokens,
                          size_t begin, size_t end) {
  for (size_t index = begin; index < end; ++index) {
    if (IsIdent(src, tokens[index], L"await") ||
        IsIdent(src, tokens[index], L"yield")) {
      return true;
    }
  }
  return false;
}

wstring ReplaceNumbers(wstring const& src, Usage&, bool& changed) {
  vector<Tok> tokens = Tokenize(src);
  vector<Edit> edits;
  for (auto const& token : tokens) {
    if (token.kind != TK::Number) {
      continue;
    }
    wstring text = Text(src, token);
    if (text.find(L'_') == wstring::npos) {
      continue;
    }
    wstring cleaned;
    cleaned.reserve(text.size());
    for (wchar_t ch : text) {
      if (ch != L'_') {
        cleaned.push_back(ch);
      }
    }
    edits.push_back({token.begin, token.end, cleaned});
  }
  if (!edits.empty()) {
    changed = true;
  }
  return ApplyEdits(src, edits);
}

wstring ReplaceNullish(wstring const& src, Usage& usage, bool& changed) {
  vector<Tok> tokens = Tokenize(src);
  vector<Edit> edits;
  for (size_t index = 0; index < tokens.size();) {
    if (!IsPunct(src, tokens[index], L"??")) {
      ++index;
      continue;
    }
    const size_t leftStart = FindLeftStart(src, tokens, index);
    vector<std::pair<size_t, size_t>> operands;
    size_t open = index;
    size_t end = index;
    while (true) {
      const size_t rightEnd = FindRightEnd(src, tokens, open);
      if (rightEnd <= open + 1) {
        end = rightEnd;
        break;
      }
      operands.push_back({tokens[open].end, tokens[rightEnd - 1].end});
      end = rightEnd;
      if (rightEnd < tokens.size() && IsPunct(src, tokens[rightEnd], L"??")) {
        open = rightEnd;
        continue;
      }
      break;
    }
    if (operands.empty()) {
      ++index;
      continue;
    }
    wstring first = src.substr(tokens[leftStart].begin,
                               tokens[index].begin - tokens[leftStart].begin);
    vector<wstring> values;
    values.push_back(first);
    for (auto const& operand : operands) {
      values.push_back(src.substr(operand.first, operand.second - operand.first));
    }
    const bool greedy = ContainsAwaitOrYield(src, tokens, index, end);
    wstring replacement;
    if (greedy) {
      for (size_t part = 0; part + 1 < values.size(); ++part) {
        replacement += L"(" + values[part] + L" !== null && " + values[part] +
                       L" !== void 0 ? " + values[part] + L" : ";
      }
      replacement += values.back();
      for (size_t part = 1; part < values.size(); ++part) {
        replacement += L")";
      }
    } else {
      usage.nullish = true;
      for (size_t part = 0; part + 1 < values.size(); ++part) {
        replacement += wstring(kNullishHelper) + L"(" + values[part] +
                       L", () => ";
      }
      replacement += values.back();
      for (size_t part = 1; part < values.size(); ++part) {
        replacement += L")";
      }
    }
    edits.push_back(
        {tokens[leftStart].begin, tokens[end - 1].end, replacement});
    changed = true;
    index = end;
  }
  return ApplyEdits(src, edits);
}

wstring ReplaceLogicalAssign(wstring const& src, Usage& usage, bool& changed) {
  vector<Tok> tokens = Tokenize(src);
  vector<Edit> edits;
  for (size_t index = 0; index < tokens.size();) {
    wchar_t const* operation = nullptr;
    if (IsPunct(src, tokens[index], L"??=")) {
      operation = L"??=";
    } else if (IsPunct(src, tokens[index], L"||=")) {
      operation = L"||=";
    } else if (IsPunct(src, tokens[index], L"&&=")) {
      operation = L"&&=";
    }
    if (operation == nullptr) {
      ++index;
      continue;
    }
    const size_t leftStart = FindPostfixStart(src, tokens, index);
    const size_t rightEnd = FindAssignRightEnd(src, tokens, index);
    if (rightEnd <= index + 1) {
      ++index;
      continue;
    }
    const wstring left =
        src.substr(tokens[leftStart].begin,
                   tokens[index].begin - tokens[leftStart].begin);
    const wstring right = src.substr(tokens[index].end,
                                     tokens[rightEnd - 1].end -
                                         tokens[index].end);
    wstring replacement;
    const bool greedy = ContainsAwaitOrYield(src, tokens, index, rightEnd);
    if (wstring(operation) == L"??=") {
      if (greedy) {
        replacement = L"(" + left + L" !== null && " + left +
                      L" !== void 0 ? " + left + L" : (" + left + L" = " +
                      right + L"))";
      } else {
        usage.nullish = true;
        replacement = wstring(kNullishHelper) + L"(" + left + L", () => " +
                      left + L" = " + right + L")";
      }
    } else if (wstring(operation) == L"||=") {
      replacement = left + L" || (" + left + L" = " + right + L")";
    } else {
      replacement = left + L" && (" + left + L" = " + right + L")";
    }
    edits.push_back({tokens[leftStart].begin, tokens[rightEnd - 1].end,
                     replacement});
    changed = true;
    index = rightEnd;
  }
  return ApplyEdits(src, edits);
}

size_t FindPostfixEnd(wstring const& src, vector<Tok> const& tokens,
                      size_t start) {
  size_t index = start;
  while (index < tokens.size()) {
    Tok const& token = tokens[index];
    if (IsPunct(src, token, L".")) {
      index += 2;
    } else if (IsPunct(src, token, L"?.")) {
      if (index + 1 < tokens.size() &&
          (IsPunct(src, tokens[index + 1], L"[") ||
           IsPunct(src, tokens[index + 1], L"("))) {
        const size_t close = FindMatchingForward(src, tokens, index + 1);
        index = close + 1;
      } else {
        index += 2;
      }
    } else if (IsPunct(src, token, L"[") || IsPunct(src, token, L"(")) {
      const size_t close = FindMatchingForward(src, tokens, index);
      index = close + 1;
    } else if (token.kind == TK::Template) {
      ++index;
    } else {
      break;
    }
  }
  return index;
}

wstring BuildOptional(wstring const& src, vector<Tok> const& tokens,
                      size_t chainStart, size_t firstOptional, size_t chainEnd,
                      Usage& usage, bool& changed) {
  wstring base = src.substr(tokens[chainStart].begin,
                            tokens[firstOptional].begin -
                                tokens[chainStart].begin);
  wstring receiver;
  if (firstOptional > chainStart) {
    Tok const& last = tokens[firstOptional - 1];
    if (IsPunct(src, last, L"]")) {
      const size_t open = FindMatchingBackward(src, tokens, firstOptional - 1);
      receiver = src.substr(tokens[chainStart].begin,
                            tokens[open].begin - tokens[chainStart].begin);
    } else if (last.kind == TK::Ident && firstOptional - chainStart >= 2 &&
               IsPunct(src, tokens[firstOptional - 2], L".")) {
      receiver = src.substr(tokens[chainStart].begin,
                            tokens[firstOptional - 2].begin -
                                tokens[chainStart].begin);
    }
  }

  struct Link {
    enum Kind { Member, Computed, Call, OptionalMember, OptionalComputed,
                OptionalCall, Tagged };
    Kind kind;
    wstring text;
    wstring inner;
  };
  vector<Link> links;
  size_t index = firstOptional;
  while (index < chainEnd) {
    Tok const& token = tokens[index];
    if (IsPunct(src, token, L"?.") && index + 1 < chainEnd &&
        IsPunct(src, tokens[index + 1], L"[")) {
      const size_t close = FindMatchingForward(src, tokens, index + 1);
      Link link{Link::OptionalComputed, L"",
                src.substr(tokens[index + 1].end,
                           tokens[close].begin - tokens[index + 1].end)};
      links.push_back(link);
      index = close + 1;
    } else if (IsPunct(src, token, L"?.") && index + 1 < chainEnd &&
               IsPunct(src, tokens[index + 1], L"(")) {
      const size_t close = FindMatchingForward(src, tokens, index + 1);
      Link link{Link::OptionalCall, L"",
                src.substr(tokens[index + 1].end,
                           tokens[close].begin - tokens[index + 1].end)};
      links.push_back(link);
      index = close + 1;
    } else if (IsPunct(src, token, L"?.") && index + 1 < chainEnd) {
      Link link{Link::OptionalMember, L".", Text(src, tokens[index + 1])};
      links.push_back(link);
      index += 2;
    } else if (IsPunct(src, token, L".") && index + 1 < chainEnd) {
      Link link{Link::Member, L"", Text(src, tokens[index + 1])};
      links.push_back(link);
      index += 2;
    } else if (IsPunct(src, token, L"[")) {
      const size_t close = FindMatchingForward(src, tokens, index);
      Link link{Link::Computed, L"",
                src.substr(token.end, tokens[close].begin - token.end)};
      links.push_back(link);
      index = close + 1;
    } else if (IsPunct(src, token, L"(")) {
      const size_t close = FindMatchingForward(src, tokens, index);
      Link link{Link::Call, L"",
                src.substr(token.end, tokens[close].begin - token.end)};
      links.push_back(link);
      index = close + 1;
    } else if (token.kind == TK::Template) {
      Link link{Link::Tagged, Text(src, token), L""};
      links.push_back(link);
      ++index;
    } else {
      break;
    }
  }
  if (links.empty()) {
    return {};
  }
  const bool greedy = ContainsAwaitOrYield(src, tokens, chainStart, chainEnd);

  struct Builder {
    vector<Link> const& links;
    Usage& usage;
    wstring const& src;

    wstring Build(size_t linkIndex, wstring current, wstring receiver) const {
      if (linkIndex >= links.size()) {
        return current;
      }
      Link const& link = links[linkIndex];
      if (link.kind == Link::Member) {
        return Build(linkIndex + 1, current + L"." + link.inner, current);
      }
      if (link.kind == Link::Computed) {
        return Build(linkIndex + 1, current + L"[" + link.inner + L"]",
                     current);
      }
      if (link.kind == Link::Call) {
        return Build(linkIndex + 1, current + L"(" + link.inner + L")", L"");
      }
      if (link.kind == Link::Tagged) {
        return Build(linkIndex + 1, current + link.text, L"");
      }

      const wstring name = L"__em_oc_v" + std::to_wstring(usage.counter++);
      wstring innerCurrent;
      wstring innerReceiver;
      if (link.kind == Link::OptionalMember) {
        innerCurrent = name + L"." + link.inner;
        innerReceiver = name;
      } else if (link.kind == Link::OptionalComputed) {
        innerCurrent = name + L"[" + link.inner + L"]";
        innerReceiver = name;
      } else {
        const wstring receiverText =
            receiver.empty() ? wstring(L"void 0") : receiver;
        innerCurrent = name + L".apply(" + receiverText + L", [" +
                       link.inner + L"])";
        innerReceiver = L"";
      }
      const wstring rest = Build(linkIndex + 1, innerCurrent, innerReceiver);
      return wstring(kOcHelper) + L"(" + current + L", (" + name +
             L") => " + rest + L")";
    }
  };

  if (greedy) {
    // Fall back to a ternary chain that keeps await/yield outside lambdas.
    wstring expression = base;
    wstring currentReceiver = receiver;
    for (size_t linkIndex = 0; linkIndex < links.size(); ++linkIndex) {
      Link const& link = links[linkIndex];
      const bool optional = link.kind >= Link::OptionalMember;
      if (optional) {
        wstring next;
        if (link.kind == Link::OptionalMember) {
          next = expression + L"." + link.inner;
        } else if (link.kind == Link::OptionalComputed) {
          next = expression + L"[" + link.inner + L"]";
        } else {
          const wstring receiverText =
              currentReceiver.empty() ? wstring(L"void 0") : currentReceiver;
          next = expression + L".apply(" + receiverText + L", [" +
                 link.inner + L"])";
        }
        expression = L"(" + expression + L" === null || " + expression +
                     L" === void 0 ? void 0 : " + next + L")";
        currentReceiver = L"";
      } else if (link.kind == Link::Member) {
        currentReceiver = expression;
        expression += L"." + link.inner;
      } else if (link.kind == Link::Computed) {
        currentReceiver = expression;
        expression += L"[" + link.inner + L"]";
      } else if (link.kind == Link::Call) {
        expression += L"(" + link.inner + L")";
        currentReceiver = L"";
      } else {
        expression += link.text;
        currentReceiver = L"";
      }
    }
    usage.optionalChain = true;
    changed = true;
    return expression;
  }

  usage.optionalChain = true;
  usage.counter += static_cast<int>(links.size()) + 1;
  Builder builder{links, usage, src};
  changed = true;
  return builder.Build(0, base, receiver);
}

wstring ReplaceOptional(wstring const& src, Usage& usage, bool& changed) {
  vector<Tok> tokens = Tokenize(src);
  vector<Edit> edits;
  for (size_t index = 0; index < tokens.size();) {
    if (!IsPunct(src, tokens[index], L"?.")) {
      ++index;
      continue;
    }
    const size_t chainStart = FindPostfixStart(src, tokens, index);
    const size_t chainEnd = FindPostfixEnd(src, tokens, index);
    if (chainEnd <= index) {
      ++index;
      continue;
    }
    const wstring replacement =
        BuildOptional(src, tokens, chainStart, index, chainEnd, usage, changed);
    if (replacement.empty()) {
      ++index;
      continue;
    }
    edits.push_back(
        {tokens[chainStart].begin, tokens[chainEnd - 1].end, replacement});
    index = chainEnd;
  }
  return ApplyEdits(src, edits);
}

bool LooksLikeMemberStart(wstring const& src, vector<Tok> const& tokens,
                          size_t index) {
  if (index >= tokens.size()) {
    return false;
  }
  Tok const& token = tokens[index];
  if (token.kind == TK::Ident || token.kind == TK::Number ||
      token.kind == TK::String) {
    return true;
  }
  if (IsPunct(src, token, L"[")) {
    return true;
  }
  return false;
}

size_t FindEnclosingParen(wstring const& src, vector<Tok> const& tokens,
                          size_t index) {
  int depth = 0;
  for (size_t cursor = index; cursor-- > 0;) {
    Tok const& token = tokens[cursor];
    if (IsPunct(src, token, L")")) {
      ++depth;
    } else if (IsPunct(src, token, L"(")) {
      if (depth == 0) {
        const size_t close = FindMatchingForward(src, tokens, cursor);
        if (close > index) {
          return cursor;
        }
      } else {
        --depth;
      }
    }
  }
  return wstring::npos;
}

bool IsParameterParen(wstring const& src, vector<Tok> const& tokens,
                      size_t open) {
  const size_t close = FindMatchingForward(src, tokens, open);
  if (close + 1 >= tokens.size()) {
    return false;
  }
  const bool arrow = IsPunct(src, tokens[close + 1], L"=>");
  const bool block = IsPunct(src, tokens[close + 1], L"{");
  if (!arrow && !block) {
    return false;
  }
  if (arrow) {
    return true;
  }
  if (open == 0) {
    return false;
  }
  Tok const& previous = tokens[open - 1];
  if (previous.kind == TK::Ident) {
    const wstring word = Text(src, previous);
    return word != L"for" && word != L"if" && word != L"while" &&
           word != L"switch" && word != L"with";
  }
  return IsPunct(src, previous, L"*") || IsPunct(src, previous, L"]") ||
         IsPunct(src, previous, L")");
}

vector<size_t> BuildBraceParents(wstring const& src,
                                 vector<Tok> const& tokens) {
  vector<size_t> parents(tokens.size(), wstring::npos);
  vector<size_t> stack;
  for (size_t index = 0; index < tokens.size(); ++index) {
    if (IsPunct(src, tokens[index], L"{")) {
      parents[index] = stack.empty() ? wstring::npos : stack.back();
      stack.push_back(index);
    } else if (IsPunct(src, tokens[index], L"}")) {
      if (!stack.empty()) {
        stack.pop_back();
      }
    }
  }
  return parents;
}

bool PatternValueHasDefault(wstring const& src, vector<Tok> const& tokens,
                            size_t parentOpen, size_t childOpen) {
  size_t partStart = parentOpen + 1;
  int depth = 0;
  for (size_t index = parentOpen + 1; index < childOpen; ++index) {
    if (IsOpen(src, tokens[index])) {
      ++depth;
    } else if (IsClose(src, tokens[index])) {
      --depth;
    } else if (depth == 0 && IsPunct(src, tokens[index], L",")) {
      partStart = index + 1;
    }
  }
  depth = 0;
  for (size_t index = partStart; index < childOpen; ++index) {
    if (IsOpen(src, tokens[index])) {
      ++depth;
    } else if (IsClose(src, tokens[index])) {
      --depth;
    } else if (depth == 0 && IsPunct(src, tokens[index], L"=")) {
      return true;
    }
  }
  return false;
}

bool IsPatternBrace(wstring const& src, vector<Tok> const& tokens,
                    vector<size_t> const& parents, size_t open) {
  const size_t close = FindMatchingForward(src, tokens, open);
  if (close + 1 < tokens.size() &&
      IsPunct(src, tokens[close + 1], L"=")) {
    return true;
  }
  if (open > 0 && tokens[open - 1].kind == TK::Ident) {
    const wstring before = Text(src, tokens[open - 1]);
    if (before == L"const" || before == L"let" || before == L"var") {
      return true;
    }
  }
  const size_t paren = FindEnclosingParen(src, tokens, open);
  if (paren != wstring::npos &&
      IsParameterParen(src, tokens, paren) && open > 0) {
    Tok const& previous = tokens[open - 1];
    if (IsPunct(src, previous, L"(") || IsPunct(src, previous, L",") ||
        IsPunct(src, previous, L"...")) {
      return true;
    }
  }
  const size_t parent = parents[open];
  if (parent != wstring::npos &&
      !PatternValueHasDefault(src, tokens, parent, open)) {
    return IsPatternBrace(src, tokens, parents, parent);
  }
  return false;
}

wstring ReplaceObjectSpread(wstring const& src, Usage& usage, bool& changed) {
  vector<Tok> tokens = Tokenize(src);
  const vector<size_t> parents = BuildBraceParents(src, tokens);
  vector<Edit> edits;
  for (size_t index = 0; index < tokens.size();) {
    if (!IsPunct(src, tokens[index], L"{")) {
      ++index;
      continue;
    }
    const size_t close = FindMatchingForward(src, tokens, index);
    bool hasSpread = false;
    int depth = 0;
    for (size_t inner = index + 1; inner < close; ++inner) {
      if (IsOpen(src, tokens[inner])) {
        ++depth;
      } else if (IsClose(src, tokens[inner])) {
        --depth;
      } else if (depth == 0 && IsPunct(src, tokens[inner], L"...")) {
        hasSpread = true;
        break;
      }
    }
    if (!hasSpread) {
      ++index;
      continue;
    }
    if (IsPatternBrace(src, tokens, parents, index)) {
      ++index;
      continue;
    }

    vector<std::pair<size_t, size_t>> parts;
    size_t partStart = index + 1;
    depth = 0;
    for (size_t inner = index + 1; inner < close; ++inner) {
      if (IsOpen(src, tokens[inner])) {
        ++depth;
      } else if (IsClose(src, tokens[inner])) {
        --depth;
      } else if (depth == 0 && IsPunct(src, tokens[inner], L",")) {
        parts.push_back({partStart, inner});
        partStart = inner + 1;
      }
    }
    if (partStart < close) {
      parts.push_back({partStart, close});
    }

    wstring replacement = wstring(kSpreadHelper) + L"({}";
    bool pending = false;
    size_t groupStart = 0;
    for (auto const& part : parts) {
      if (part.first >= part.second) {
        continue;
      }
      const bool spread = IsPunct(src, tokens[part.first], L"...");
      if (spread) {
        if (pending) {
          replacement += L", {" +
                         src.substr(tokens[groupStart].begin,
                                    tokens[part.first].begin -
                                        tokens[groupStart].begin) +
                         L"}";
          pending = false;
        }
        const size_t operandStart =
            part.first + 1 < part.second ? tokens[part.first + 1].begin : 0;
        replacement += L", " +
                       src.substr(operandStart,
                                  tokens[part.second - 1].end - operandStart);
      } else {
        if (!pending) {
          pending = true;
          groupStart = part.first;
        }
      }
    }
    if (pending) {
      replacement += L", {" +
                     src.substr(tokens[groupStart].begin,
                                tokens[close].begin -
                                    tokens[groupStart].begin) +
                     L"}";
    }
    replacement += L")";
    edits.push_back({tokens[index].begin, tokens[close].end, replacement});
    usage.spread = true;
    changed = true;
    index = close + 1;
  }
  return ApplyEdits(src, edits);
}

wstring PatternKeys(wstring const& src, vector<Tok> const& tokens,
                    size_t open, size_t close) {
  wstring keys;
  int depth = 0;
  size_t partStart = open + 1;
  vector<std::pair<size_t, size_t>> parts;
  for (size_t index = open + 1; index < close; ++index) {
    if (IsOpen(src, tokens[index])) {
      ++depth;
    } else if (IsClose(src, tokens[index])) {
      --depth;
    } else if (depth == 0 && IsPunct(src, tokens[index], L",")) {
      parts.push_back({partStart, index});
      partStart = index + 1;
    }
  }
  if (partStart < close) {
    parts.push_back({partStart, close});
  }
  for (auto const& part : parts) {
    if (part.first >= part.second) {
      continue;
    }
    Tok const& first = tokens[part.first];
    if (IsPunct(src, first, L"...")) {
      continue;
    }
    if (IsPunct(src, first, L"[") ) {
      const size_t bracket = FindMatchingForward(src, tokens, part.first);
      wstring expression =
          src.substr(first.end, tokens[bracket].begin - first.end);
      if (!keys.empty()) {
        keys += L", ";
      }
      keys += L"(" + expression + L")";
      continue;
    }
    if (first.kind == TK::Ident) {
      wstring key = Text(src, first);
      if (!keys.empty()) {
        keys += L", ";
      }
      keys += L"\"" + key + L"\"";
      continue;
    }
    if (first.kind == TK::String) {
      if (!keys.empty()) {
        keys += L", ";
      }
      keys += Text(src, first);
      continue;
    }
    if (first.kind == TK::Number) {
      if (!keys.empty()) {
        keys += L", ";
      }
      keys += L"String(" + Text(src, first) + L")";
    }
  }
  return keys;
}

struct RestJob {
  wstring temp;
  size_t open{0};
  size_t close{0};
};

struct PatternPlan {
  wstring temp;
  bool object{false};
  wstring body;
  bool hasRest{false};
  wstring restName;
  wstring keys;
  vector<RestJob> children;
};

struct RestPlanSet {
  vector<PatternPlan> plans;
  vector<wstring> temps;
};

size_t FindElementEnd(wstring const& src, vector<Tok> const& tokens,
                      size_t start, size_t close) {
  int depth = 0;
  for (size_t index = start; index < close; ++index) {
    if (IsOpen(src, tokens[index])) {
      ++depth;
    } else if (IsClose(src, tokens[index])) {
      --depth;
    } else if (depth == 0 && IsPunct(src, tokens[index], L",")) {
      return index;
    }
  }
  return close;
}

bool PatternContainsRest(wstring const& src, vector<Tok> const& tokens,
                         size_t open, size_t close) {
  const bool object = IsPunct(src, tokens[open], L"{");
  size_t index = open + 1;
  while (index < close) {
    if (IsPunct(src, tokens[index], L",")) {
      ++index;
      continue;
    }
    if (object && IsPunct(src, tokens[index], L"...")) {
      return true;
    }
    if (!object && IsPunct(src, tokens[index], L"...")) {
      index = FindElementEnd(src, tokens, index + 1, close);
      continue;
    }
    size_t valueStart = wstring::npos;
    if (object) {
      if (IsPunct(src, tokens[index], L"[")) {
        const size_t keyClose = FindMatchingForward(src, tokens, index);
        if (keyClose >= close) {
          return false;
        }
        index = keyClose + 1;
        if (index < close && IsPunct(src, tokens[index], L":")) {
          valueStart = index + 1;
        }
      } else {
        ++index;
        if (index < close && IsPunct(src, tokens[index], L":")) {
          valueStart = index + 1;
        }
      }
    } else {
      valueStart = index;
    }
    if (valueStart != wstring::npos && valueStart < close &&
        (IsPunct(src, tokens[valueStart], L"{") ||
         IsPunct(src, tokens[valueStart], L"["))) {
      const size_t childClose =
          FindMatchingForward(src, tokens, valueStart);
      if (childClose >= close) {
        return false;
      }
      if (PatternContainsRest(src, tokens, valueStart, childClose)) {
        return true;
      }
      index = childClose + 1;
    } else if (valueStart != wstring::npos) {
      ++index;
    }
    index = FindElementEnd(src, tokens, index, close);
  }
  return false;
}

wstring BuildPatternBody(wstring const& src, vector<Tok> const& tokens,
                         size_t open, size_t close, bool object,
                         bool& hasRest, wstring& restName, vector<RestJob>& jobs,
                         Usage& usage) {
  const size_t base = tokens[open].end;
  vector<Edit> edits;
  hasRest = false;
  restName.clear();
  size_t index = open + 1;
  while (index < close) {
    if (IsPunct(src, tokens[index], L",")) {
      ++index;
      continue;
    }
    if (object && IsPunct(src, tokens[index], L"...")) {
      const size_t elementEnd =
          FindElementEnd(src, tokens, index + 1, close);
      if (index + 1 < elementEnd) {
        restName = src.substr(tokens[index + 1].begin,
                              tokens[elementEnd - 1].end -
                                  tokens[index + 1].begin);
        edits.push_back({tokens[index].begin - base,
                         tokens[elementEnd - 1].end - base, L""});
        hasRest = true;
      }
      index = elementEnd;
      continue;
    }
    if (!object && IsPunct(src, tokens[index], L"...")) {
      index = FindElementEnd(src, tokens, index + 1, close);
      continue;
    }
    size_t valueStart = wstring::npos;
    if (object) {
      if (IsPunct(src, tokens[index], L"[")) {
        const size_t keyClose = FindMatchingForward(src, tokens, index);
        if (keyClose >= close) {
          break;
        }
        index = keyClose + 1;
        if (index < close && IsPunct(src, tokens[index], L":")) {
          valueStart = index + 1;
        }
      } else {
        ++index;
        if (index < close && IsPunct(src, tokens[index], L":")) {
          valueStart = index + 1;
        }
      }
    } else {
      valueStart = index;
    }
    if (valueStart != wstring::npos && valueStart < close &&
        (IsPunct(src, tokens[valueStart], L"{") ||
         IsPunct(src, tokens[valueStart], L"["))) {
      const size_t childClose =
          FindMatchingForward(src, tokens, valueStart);
      if (childClose < close) {
        if (PatternContainsRest(src, tokens, valueStart, childClose)) {
          if (IsPunct(src, tokens[valueStart], L"{")) {
            const wstring temp =
                L"__em_rest_" + std::to_wstring(usage.counter++);
            jobs.push_back({temp, valueStart, childClose});
            edits.push_back({tokens[valueStart].begin - base,
                             tokens[childClose].end - base, temp});
          } else {
            bool childHasRest = false;
            wstring childRest;
            vector<RestJob> childJobs;
            const wstring childBody = BuildPatternBody(
                src, tokens, valueStart, childClose, false, childHasRest,
                childRest, childJobs, usage);
            jobs.insert(jobs.end(), childJobs.begin(), childJobs.end());
            edits.push_back({tokens[valueStart].begin - base,
                             tokens[childClose].end - base,
                             L"[" + childBody + L"]"});
          }
        }
        index = childClose + 1;
      } else {
        ++index;
      }
    } else if (valueStart != wstring::npos) {
      ++index;
    }
    index = FindElementEnd(src, tokens, index, close);
  }
  return ApplyEdits(src.substr(base, tokens[close].begin - base), edits);
}

PatternPlan MakePatternPlan(wstring const& src, vector<Tok> const& tokens,
                            wstring const& temp, size_t open, size_t close,
                            Usage& usage) {
  PatternPlan plan;
  plan.temp = temp;
  plan.object = IsPunct(src, tokens[open], L"{");
  plan.body = BuildPatternBody(src, tokens, open, close, plan.object,
                               plan.hasRest, plan.restName, plan.children,
                               usage);
  if (plan.hasRest) {
    plan.keys = PatternKeys(src, tokens, open, close);
  }
  return plan;
}

RestPlanSet MakePlanSet(wstring const& src, vector<Tok> const& tokens,
                        PatternPlan root, Usage& usage) {
  RestPlanSet set;
  vector<PatternPlan> queue;
  queue.push_back(std::move(root));
  for (size_t head = 0; head < queue.size(); ++head) {
    PatternPlan plan = std::move(queue[head]);
    if (plan.hasRest) {
      usage.rest = true;
    }
    for (auto const& child : plan.children) {
      set.temps.push_back(child.temp);
      queue.push_back(MakePatternPlan(src, tokens, child.temp, child.open,
                                      child.close, usage));
    }
    set.plans.push_back(std::move(plan));
  }
  return set;
}

wstring PatternLiteral(PatternPlan const& plan) {
  if (plan.object) {
    return L"{" + plan.body + L"}";
  }
  return L"[" + plan.body + L"]";
}

wstring BuildDeclaratorChain(vector<PatternPlan> const& plans) {
  wstring chain;
  for (auto const& plan : plans) {
    if (!chain.empty()) {
      chain += L", ";
    }
    chain += PatternLiteral(plan) + L" = " + plan.temp;
    if (plan.hasRest) {
      chain += L", " + plan.restName + L" = " + wstring(kRestHelper) + L"(" +
               plan.temp + L", [" + plan.keys + L"])";
    }
  }
  return chain;
}

wstring BuildTempDeclarations(vector<wstring> const& temps) {
  if (temps.empty()) {
    return L"";
  }
  wstring text = L"var ";
  for (size_t index = 0; index < temps.size(); ++index) {
    if (index > 0) {
      text += L", ";
    }
    text += temps[index];
  }
  return text + L";";
}

wstring BuildAssignmentStatements(vector<PatternPlan> const& plans) {
  wstring text;
  for (auto const& plan : plans) {
    text += L"(" + PatternLiteral(plan) + L" = " + plan.temp + L");";
    if (plan.hasRest) {
      text += plan.restName + L" = " + wstring(kRestHelper) + L"(" +
              plan.temp + L", [" + plan.keys + L"]);";
    }
  }
  return text;
}

size_t DirectiveInsertOffset(wstring const& src, vector<Tok> const& tokens,
                             size_t open, size_t close) {
  size_t index = open + 1;
  size_t insert = tokens[open].end;
  while (index < close && tokens[index].kind == TK::String) {
    const size_t after = index + 1;
    if (after < close && IsPunct(src, tokens[after], L";")) {
      insert = tokens[after].end;
      index = after + 1;
      continue;
    }
    if (after >= close) {
      insert = tokens[index].end;
      break;
    }
    Tok const& next = tokens[after];
    bool continues = false;
    if (next.kind == TK::Punct) {
      continues = !IsPunct(src, next, L"}") && !IsPunct(src, next, L";") &&
                  !IsPunct(src, next, L"{");
    } else if (next.kind == TK::Regex || next.kind == TK::Template) {
      continues = true;
    } else if (!next.nl) {
      continues = true;
    }
    if (continues) {
      break;
    }
    insert = tokens[index].end;
    index = after;
  }
  return insert;
}

size_t TopLevelDirectiveInsert(wstring const& src, vector<Tok> const& tokens) {
  size_t index = 0;
  size_t insert = 0;
  while (index < tokens.size() && tokens[index].kind == TK::String) {
    const size_t after = index + 1;
    if (after < tokens.size() && IsPunct(src, tokens[after], L";")) {
      insert = tokens[after].end;
      index = after + 1;
      continue;
    }
    if (after >= tokens.size()) {
      insert = tokens[index].end;
      break;
    }
    Tok const& next = tokens[after];
    bool continues = false;
    if (next.kind == TK::Punct) {
      continues = !IsPunct(src, next, L"}") && !IsPunct(src, next, L";") &&
                  !IsPunct(src, next, L"{");
    } else if (next.kind == TK::Regex || next.kind == TK::Template) {
      continues = true;
    } else if (!next.nl) {
      continues = true;
    }
    if (continues) {
      break;
    }
    insert = tokens[index].end;
    index = after;
  }
  return insert;
}

bool IsComplexBodyStart(wstring const& src, vector<Tok> const& tokens,
                        size_t index) {
  if (index >= tokens.size()) {
    return true;
  }
  Tok const& tok = tokens[index];
  if (IsIdent(src, tok, L"if") || IsIdent(src, tok, L"do") ||
      IsIdent(src, tok, L"for") || IsIdent(src, tok, L"while") ||
      IsIdent(src, tok, L"switch") || IsIdent(src, tok, L"try") ||
      IsIdent(src, tok, L"with") || IsIdent(src, tok, L"function") ||
      IsIdent(src, tok, L"class") || IsIdent(src, tok, L"import") ||
      IsIdent(src, tok, L"export")) {
    return true;
  }
  if (IsIdent(src, tok, L"async") && index + 1 < tokens.size() &&
      IsIdent(src, tokens[index + 1], L"function")) {
    return true;
  }
  return tok.kind == TK::Ident && index + 1 < tokens.size() &&
         IsPunct(src, tokens[index + 1], L":");
}

bool EndsStatement(wstring const& src, Tok const& tok) {
  if (tok.kind == TK::Ident || tok.kind == TK::Number ||
      tok.kind == TK::String || tok.kind == TK::Template ||
      tok.kind == TK::Regex) {
    return true;
  }
  return tok.kind == TK::Punct &&
         (IsPunct(src, tok, L")") || IsPunct(src, tok, L"]") ||
          IsPunct(src, tok, L"}") || IsPunct(src, tok, L"++") ||
          IsPunct(src, tok, L"--"));
}

bool StartsNewStatement(wstring const& src, Tok const& tok) {
  if (tok.kind == TK::Ident) {
    return !IsIdent(src, tok, L"in") && !IsIdent(src, tok, L"instanceof");
  }
  if (tok.kind == TK::Number || tok.kind == TK::String) {
    return true;
  }
  return tok.kind == TK::Punct &&
         (IsPunct(src, tok, L"{") || IsPunct(src, tok, L"!") ||
          IsPunct(src, tok, L"~") || IsPunct(src, tok, L"++") ||
          IsPunct(src, tok, L"--"));
}

wstring ReplaceForOfRest(wstring const& src, Usage& usage, bool& changed) {
  vector<Tok> tokens = Tokenize(src);
  vector<Edit> edits;
  for (size_t index = 0; index < tokens.size();) {
    if (!IsIdent(src, tokens[index], L"for")) {
      ++index;
      continue;
    }
    size_t cursor = index + 1;
    if (cursor < tokens.size() && IsIdent(src, tokens[cursor], L"await")) {
      ++cursor;
    }
    if (cursor >= tokens.size() || !IsPunct(src, tokens[cursor], L"(")) {
      ++index;
      continue;
    }
    const size_t headerOpen = cursor;
    const size_t headerClose = FindMatchingForward(src, tokens, headerOpen);
    if (headerClose <= headerOpen || headerClose >= tokens.size()) {
      ++index;
      continue;
    }
    size_t patternStart = headerOpen + 1;
    wstring kind;
    if (patternStart < headerClose && tokens[patternStart].kind == TK::Ident) {
      const wstring word = Text(src, tokens[patternStart]);
      if (word == L"const" || word == L"let" || word == L"var") {
        kind = word;
        ++patternStart;
      }
    }
    if (patternStart >= headerClose ||
        !(IsPunct(src, tokens[patternStart], L"{") ||
          IsPunct(src, tokens[patternStart], L"["))) {
      ++index;
      continue;
    }
    const size_t patternClose =
        FindMatchingForward(src, tokens, patternStart);
    if (patternClose >= headerClose) {
      ++index;
      continue;
    }
    if (patternClose + 1 >= headerClose ||
        !(IsIdent(src, tokens[patternClose + 1], L"of") ||
          IsIdent(src, tokens[patternClose + 1], L"in"))) {
      ++index;
      continue;
    }
    if (!PatternContainsRest(src, tokens, patternStart, patternClose)) {
      ++index;
      continue;
    }
    const size_t bodyStart = headerClose + 1;
    if (bodyStart >= tokens.size()) {
      ++index;
      continue;
    }
    bool blockBody = IsPunct(src, tokens[bodyStart], L"{");
    if (!blockBody &&
        IsComplexBodyStart(src, tokens, bodyStart)) {
      ++index;
      continue;
    }
    size_t bodyClose = bodyStart;
    size_t statementEnd = bodyStart;
    if (blockBody) {
      bodyClose = FindMatchingForward(src, tokens, bodyStart);
      if (bodyClose <= bodyStart || bodyClose >= tokens.size()) {
        ++index;
        continue;
      }
    } else {
      int depth = 0;
      bool foundEnd = false;
      bool unsafe = false;
      size_t scan = bodyStart;
      for (; scan < tokens.size(); ++scan) {
        if (depth == 0 && scan > bodyStart && tokens[scan].nl &&
            EndsStatement(src, tokens[scan - 1]) &&
            StartsNewStatement(src, tokens[scan])) {
          unsafe = true;
          break;
        }
        if (depth == 0 && IsIdent(src, tokens[scan], L"do")) {
          unsafe = true;
          break;
        }
        if (IsOpen(src, tokens[scan])) {
          ++depth;
        } else if (IsClose(src, tokens[scan])) {
          if (depth == 0) {
            if (scan > bodyStart) {
              statementEnd = scan - 1;
              foundEnd = true;
            }
            break;
          }
          --depth;
        } else if (depth == 0 && IsPunct(src, tokens[scan], L";")) {
          statementEnd = scan;
          foundEnd = true;
          break;
        }
      }
      if (!foundEnd && !unsafe && scan >= tokens.size() && scan > bodyStart) {
        statementEnd = tokens.size() - 1;
        foundEnd = true;
      }
      if (!foundEnd || unsafe) {
        ++index;
        continue;
      }
      if (statementEnd + 1 < tokens.size() &&
          IsIdent(src, tokens[statementEnd + 1], L"else")) {
        ++index;
        continue;
      }
    }
    const wstring temp = L"__em_rest_" + std::to_wstring(usage.counter++);
    PatternPlan root =
        MakePatternPlan(src, tokens, temp, patternStart, patternClose, usage);
    RestPlanSet set = MakePlanSet(src, tokens, root, usage);
    wstring statements;
    if (!kind.empty()) {
      statements = kind + L" " + BuildDeclaratorChain(set.plans) + L";";
    } else {
      statements = BuildTempDeclarations(set.temps) +
                   BuildAssignmentStatements(set.plans);
    }
    edits.push_back({tokens[patternStart].begin, tokens[patternClose].end,
                     kind.empty() ? L"const " + temp : temp});
    if (blockBody) {
      edits.push_back({tokens[bodyStart].end, tokens[bodyStart].end,
                       statements});
    } else {
      edits.push_back({tokens[bodyStart].begin, tokens[bodyStart].begin,
                       L"{" + statements});
      edits.push_back({tokens[statementEnd].end, tokens[statementEnd].end,
                       L"}"});
    }
    usage.rest = true;
    changed = true;
    index = blockBody ? bodyClose + 1 : statementEnd + 1;
  }
  return ApplyEdits(src, edits);
}

wstring ReplaceRestAssignments(wstring const& src, Usage& usage,
                               bool& changed) {
  vector<Tok> tokens = Tokenize(src);
  vector<Edit> edits;
  for (size_t index = 0; index < tokens.size();) {
    if (!(IsPunct(src, tokens[index], L"{") ||
          IsPunct(src, tokens[index], L"["))) {
      ++index;
      continue;
    }
    const size_t close = FindMatchingForward(src, tokens, index);
    if (close <= index || close + 1 >= tokens.size() ||
        !IsPunct(src, tokens[close + 1], L"=")) {
      ++index;
      continue;
    }
    if (index > 0 && tokens[index - 1].kind == TK::Ident) {
      const wstring before = Text(src, tokens[index - 1]);
      if (before == L"const" || before == L"let" || before == L"var") {
        ++index;
        continue;
      }
    }
    const size_t paren = FindEnclosingParen(src, tokens, index);
    if (paren != wstring::npos &&
        IsParameterParen(src, tokens, paren)) {
      ++index;
      continue;
    }
    if (!PatternContainsRest(src, tokens, index, close)) {
      ++index;
      continue;
    }
    const size_t equals = close + 1;
    const size_t valueEnd = FindAssignRightEnd(src, tokens, equals);
    if (valueEnd <= equals + 1) {
      ++index;
      continue;
    }
    const wstring temp = L"__em_rest_" + std::to_wstring(usage.counter++);
    PatternPlan root =
        MakePatternPlan(src, tokens, temp, index, close, usage);
    RestPlanSet set = MakePlanSet(src, tokens, root, usage);
    const wstring value = src.substr(
        tokens[equals + 1].begin,
        tokens[valueEnd - 1].end - tokens[equals + 1].begin);
    const wstring replacement =
        L"((" + temp + L") => {" + BuildTempDeclarations(set.temps) +
        BuildAssignmentStatements(set.plans) + L"return " + temp + L";})(" +
        value + L")";
    edits.push_back({tokens[index].begin, tokens[valueEnd - 1].end,
                     replacement});
    usage.rest = true;
    changed = true;
    index = valueEnd;
  }
  return ApplyEdits(src, edits);
}

wstring ReplaceObjectRest(wstring const& src, Usage& usage, bool& changed) {
  vector<Tok> tokens = Tokenize(src);
  vector<Edit> edits;
  for (size_t index = 0; index < tokens.size();) {
    if (!(tokens[index].kind == TK::Ident &&
          (Text(src, tokens[index]) == L"const" ||
           Text(src, tokens[index]) == L"let" ||
           Text(src, tokens[index]) == L"var"))) {
      ++index;
      continue;
    }
    size_t cursor = index + 1;
    while (cursor < tokens.size()) {
      if (IsPunct(src, tokens[cursor], L";")) {
        break;
      }
      if (tokens[cursor].kind == TK::Ident && cursor > index + 1 &&
          tokens[cursor].nl) {
        break;
      }
      if (IsPunct(src, tokens[cursor], L"{") ||
          IsPunct(src, tokens[cursor], L"[")) {
        const size_t close = FindMatchingForward(src, tokens, cursor);
        const bool matched =
            close > cursor && close < tokens.size() &&
            (IsPunct(src, tokens[cursor], L"{")
                 ? IsPunct(src, tokens[close], L"}")
                 : IsPunct(src, tokens[close], L"]"));
        if (matched && PatternContainsRest(src, tokens, cursor, close)) {
          const size_t equals = close + 1;
          if (equals < tokens.size() && IsPunct(src, tokens[equals], L"=")) {
            const size_t valueEnd = FindAssignRightEnd(src, tokens, equals);
            if (valueEnd > equals + 1) {
              const wstring temp =
                  L"__em_rest_" + std::to_wstring(usage.counter++);
              PatternPlan root =
                  MakePatternPlan(src, tokens, temp, cursor, close, usage);
              RestPlanSet set = MakePlanSet(src, tokens, root, usage);
              const wstring value = src.substr(
                  tokens[equals + 1].begin,
                  tokens[valueEnd - 1].end - tokens[equals + 1].begin);
              const wstring replacement =
                  temp + L" = " + value + L", " +
                  BuildDeclaratorChain(set.plans);
              edits.push_back({tokens[cursor].begin,
                               tokens[valueEnd - 1].end, replacement});
              usage.rest = true;
              changed = true;
              cursor = valueEnd;
              if (cursor < tokens.size() &&
                  IsPunct(src, tokens[cursor], L",")) {
                ++cursor;
                continue;
              }
              break;
            }
          }
        }
        if (!matched) {
          break;
        }
        if (close + 1 < tokens.size() &&
            (IsIdent(src, tokens[close + 1], L"of") ||
             IsIdent(src, tokens[close + 1], L"in"))) {
          cursor = close + 1;
          break;
        }
        cursor = close + 1;
      }
      int depth = 0;
      for (; cursor < tokens.size(); ++cursor) {
        if (IsOpen(src, tokens[cursor])) {
          ++depth;
        } else if (IsClose(src, tokens[cursor])) {
          if (depth == 0) {
            break;
          }
          --depth;
        } else if (depth == 0 &&
                   (IsPunct(src, tokens[cursor], L",") ||
                    IsPunct(src, tokens[cursor], L";"))) {
          break;
        }
      }
      if (cursor < tokens.size() && IsPunct(src, tokens[cursor], L",")) {
        ++cursor;
        continue;
      }
      break;
    }
    index = cursor > index ? cursor : index + 1;
  }
  return ApplyEdits(src, edits);
}

wstring ReplaceParamRest(wstring const& src, Usage& usage, bool& changed) {
  vector<Tok> tokens = Tokenize(src);
  vector<Edit> edits;
  for (size_t index = 0; index < tokens.size();) {
    if (!IsPunct(src, tokens[index], L"(") ||
        !IsParameterParen(src, tokens, index)) {
      ++index;
      continue;
    }
    const size_t close = FindMatchingForward(src, tokens, index);
    if (close <= index || close >= tokens.size()) {
      ++index;
      continue;
    }
    size_t bodyOpen = wstring::npos;
    size_t bodyStart = wstring::npos;
    size_t bodyEnd = wstring::npos;
    bool expressionBody = false;
    if (close + 1 < tokens.size() &&
        IsPunct(src, tokens[close + 1], L"=>")) {
      const size_t after = close + 2;
      if (after < tokens.size() && IsPunct(src, tokens[after], L"{")) {
        bodyOpen = after;
      } else if (after < tokens.size()) {
        bodyStart = after;
        expressionBody = true;
        bodyEnd = FindAssignRightEnd(src, tokens, close + 1);
        if (bodyEnd <= bodyStart) {
          ++index;
          continue;
        }
      }
    } else if (close + 1 < tokens.size() &&
               IsPunct(src, tokens[close + 1], L"{")) {
      bodyOpen = close + 1;
    }
    if (bodyOpen == wstring::npos && !expressionBody) {
      ++index;
      continue;
    }

    vector<std::pair<size_t, size_t>> parts;
    int depth = 0;
    size_t partStart = index + 1;
    for (size_t scan = index + 1; scan < close; ++scan) {
      if (IsOpen(src, tokens[scan])) {
        ++depth;
      } else if (IsClose(src, tokens[scan])) {
        --depth;
      } else if (depth == 0 && IsPunct(src, tokens[scan], L",")) {
        parts.push_back({partStart, scan});
        partStart = scan + 1;
      }
    }
    if (partStart < close) {
      parts.push_back({partStart, close});
    }

    wstring chain;
    bool found = false;
    for (auto const& part : parts) {
      if (part.first >= part.second ||
          !(IsPunct(src, tokens[part.first], L"{") ||
            IsPunct(src, tokens[part.first], L"["))) {
        continue;
      }
      const size_t patternClose =
          FindMatchingForward(src, tokens, part.first);
      if (patternClose >= part.second) {
        continue;
      }
      if (!PatternContainsRest(src, tokens, part.first, patternClose)) {
        continue;
      }
      const wstring temp =
          L"__em_rest_" + std::to_wstring(usage.counter++);
      PatternPlan root = MakePatternPlan(src, tokens, temp, part.first,
                                         patternClose, usage);
      RestPlanSet set = MakePlanSet(src, tokens, root, usage);
      if (!chain.empty()) {
        chain += L", ";
      }
      chain += BuildDeclaratorChain(set.plans);
      edits.push_back({tokens[part.first].begin, tokens[patternClose].end,
                       temp});
      found = true;
    }
    if (!found) {
      ++index;
      continue;
    }
    const wstring body = L"var " + chain + L";";
    if (bodyOpen != wstring::npos) {
      const size_t bodyClose =
          FindMatchingForward(src, tokens, bodyOpen);
      const size_t insert = DirectiveInsertOffset(
          src, tokens, bodyOpen,
          bodyClose > bodyOpen ? bodyClose : tokens.size());
      edits.push_back({insert, insert, L";" + body});
    } else {
      const wstring expression =
          src.substr(tokens[bodyStart].begin,
                     tokens[bodyEnd - 1].end - tokens[bodyStart].begin);
      edits.push_back({tokens[bodyStart].begin, tokens[bodyEnd - 1].end,
                       L"{" + body + L"return (" + expression + L");}"});
    }
    changed = true;
    index = close + 1;
  }
  return ApplyEdits(src, edits);
}

struct ClassField {
  bool isStatic{false};
  wstring accessor;
  wstring initializer;
  size_t begin{0};
  size_t end{0};
};

wstring PrivateName(wstring const& src, int sequence, wstring const& token) {
  wstring name = token;
  if (!name.empty() && name[0] == L'#') {
    name = name.substr(1);
  }
  return L"__em_priv_" + std::to_wstring(sequence) + L"_" + name;
}

bool IsClassMemberStart(wstring const& src, vector<Tok> const& tokens,
                        size_t index) {
  if (index >= tokens.size()) {
    return false;
  }
  Tok const& token = tokens[index];
  if (token.kind == TK::Ident || token.kind == TK::String ||
      token.kind == TK::Number) {
    return true;
  }
  return IsPunct(src, token, L"[");
}

bool ClassIsDeclaration(wstring const& src, vector<Tok> const& tokens,
                        size_t index) {
  if (index == 0) {
    return true;
  }
  Tok const& previous = tokens[index - 1];
  if (IsPunct(src, previous, L";") || IsPunct(src, previous, L"{") ||
      IsPunct(src, previous, L"}")) {
    return true;
  }
  if (previous.kind == TK::Ident) {
    const wstring word = Text(src, previous);
    return word == L"export" || word == L"default";
  }
  return false;
}

wstring RenamePrivateText(wstring const& text, int sequence) {
  vector<Tok> tokens = Tokenize(text);
  vector<Edit> edits;
  for (auto const& token : tokens) {
    if (IsPrivateName(text, token)) {
      edits.push_back({token.begin, token.end,
                       PrivateName(text, sequence, Text(text, token))});
    }
  }
  return ApplyEdits(text, edits);
}

wstring ReplaceClassFields(wstring const& src, Usage& usage, bool& changed) {
  vector<Tok> tokens = Tokenize(src);
  vector<Edit> edits;
  for (size_t index = 0; index < tokens.size();) {
    if (!(tokens[index].kind == TK::Ident &&
          Text(src, tokens[index]) == L"class")) {
      ++index;
      continue;
    }
    if (index > 0 && IsPunct(src, tokens[index - 1], L".")) {
      ++index;
      continue;
    }
    if (index + 1 >= tokens.size()) {
      break;
    }
    const size_t after = index + 1;
    const bool named =
        tokens[after].kind == TK::Ident && Text(src, tokens[after]) != L"extends";
    if (!named && !IsPunct(src, tokens[after], L"{") &&
        !IsIdent(src, tokens[after], L"extends")) {
      ++index;
      continue;
    }

    wstring className;
    bool hasName = false;
    size_t cursor = after;
    if (named) {
      className = Text(src, tokens[cursor]);
      hasName = true;
      ++cursor;
    }
    bool derived = false;
    size_t bodyOpen = wstring::npos;
    int depth = 0;
    for (; cursor < tokens.size(); ++cursor) {
      Tok const& token = tokens[cursor];
      if (IsIdent(src, token, L"extends") && depth == 0) {
        derived = true;
        continue;
      }
      if (IsOpen(src, token)) {
        if (depth == 0 && IsPunct(src, token, L"{")) {
          bodyOpen = cursor;
          break;
        }
        ++depth;
      } else if (IsClose(src, token)) {
        if (depth > 0) {
          --depth;
        }
      } else if (IsPunct(src, token, L";") && depth == 0) {
        break;
      }
    }
    if (bodyOpen == wstring::npos) {
      ++index;
      continue;
    }
    const size_t bodyClose = FindMatchingForward(src, tokens, bodyOpen);
    if (bodyClose <= bodyOpen || bodyClose >= tokens.size()) {
      ++index;
      continue;
    }

    const int sequence = usage.counter++;
    vector<ClassField> fields;
    vector<wstring> staticBlocks;
    vector<std::pair<size_t, size_t>> blockRemovals;
    size_t constructorOpen = wstring::npos;
    size_t constructorClose = wstring::npos;

    size_t member = bodyOpen + 1;
    while (member < bodyClose) {
      if (IsPunct(src, tokens[member], L";")) {
        ++member;
        continue;
      }
      const size_t memberBegin = tokens[member].begin;
      if (tokens[member].kind == TK::Ident &&
          Text(src, tokens[member]) == L"static" &&
          member + 1 < bodyClose &&
          IsPunct(src, tokens[member + 1], L"{")) {
        const size_t open = member + 1;
        const size_t close = FindMatchingForward(src, tokens, open);
        staticBlocks.push_back(src.substr(tokens[open].end,
                                          tokens[close].begin -
                                              tokens[open].end));
        blockRemovals.push_back({memberBegin, tokens[close].end});
        member = close + 1;
        continue;
      }
      bool isStatic = false;
      while (member < bodyClose && tokens[member].kind == TK::Ident) {
        const wstring word = Text(src, tokens[member]);
        const size_t next = member + 1;
        const bool nextStartsMember =
            next < bodyClose &&
            !IsPunct(src, tokens[next], L"(") &&
            !IsPunct(src, tokens[next], L"=") &&
            !IsPunct(src, tokens[next], L";") &&
            !IsPunct(src, tokens[next], L"}");
        if (word == L"static" && nextStartsMember) {
          isStatic = true;
          ++member;
          continue;
        }
        if ((word == L"async" || word == L"get" || word == L"set") &&
            nextStartsMember) {
          ++member;
          continue;
        }
        break;
      }
      if (member < bodyClose && IsPunct(src, tokens[member], L"*")) {
        ++member;
      }
      if (member >= bodyClose) {
        break;
      }

      wstring accessor;
      bool isPrivate = false;
      bool isConstructor = false;
      if (IsPunct(src, tokens[member], L"[")) {
        const size_t bracket = FindMatchingForward(src, tokens, member);
        if (bracket >= bodyClose) {
          break;
        }
        accessor =
            L"[" +
            RenamePrivateText(
                src.substr(tokens[member].end,
                           tokens[bracket].begin - tokens[member].end),
                sequence) +
            L"]";
        member = bracket + 1;
      } else if (tokens[member].kind == TK::Ident) {
        wstring name = Text(src, tokens[member]);
        isConstructor = !isStatic && name == L"constructor";
        if (!name.empty() && name[0] == L'#') {
          isPrivate = true;
          accessor = L"." + PrivateName(src, sequence, name);
        } else {
          accessor = L"." + name;
        }
        ++member;
      } else if (tokens[member].kind == TK::String ||
                 tokens[member].kind == TK::Number) {
        accessor = L"[" + Text(src, tokens[member]) + L"]";
        ++member;
      } else {
        ++member;
        continue;
      }
      (void)isPrivate;

      if (member < bodyClose && IsPunct(src, tokens[member], L"(")) {
        const size_t paren = FindMatchingForward(src, tokens, member);
        if (paren >= bodyClose) {
          break;
        }
        size_t block = paren + 1;
        while (block < bodyClose && !IsPunct(src, tokens[block], L"{") &&
               !IsPunct(src, tokens[block], L";")) {
          ++block;
        }
        if (block < bodyClose && IsPunct(src, tokens[block], L"{")) {
          const size_t close = FindMatchingForward(src, tokens, block);
          if (isConstructor) {
            constructorOpen = block;
            constructorClose = close;
          }
          member = close + 1;
          continue;
        }
        member = paren + 1;
        continue;
      }

      ClassField field;
      field.isStatic = isStatic;
      field.accessor = accessor;
      field.begin = memberBegin;
      size_t initializerStart = wstring::npos;
      size_t initializerEnd = wstring::npos;
      if (member < bodyClose && IsPunct(src, tokens[member], L"=")) {
        initializerStart = member + 1;
        size_t scan = initializerStart;
        int inner = 0;
        for (; scan < bodyClose; ++scan) {
          Tok const& token = tokens[scan];
          if (IsOpen(src, token)) {
            ++inner;
          } else if (IsClose(src, token)) {
            if (inner == 0) {
              break;
            }
            --inner;
          } else if (inner == 0 && IsPunct(src, token, L";")) {
            break;
          } else if (inner == 0 && token.nl && scan > initializerStart &&
                     IsClassMemberStart(src, tokens, scan) &&
                     !IsPunct(src, tokens[scan - 1], L"=") &&
                     !IsPunct(src, tokens[scan - 1], L",") &&
                     !IsPunct(src, tokens[scan - 1], L"+") &&
                     !IsPunct(src, tokens[scan - 1], L"-") &&
                     !IsPunct(src, tokens[scan - 1], L"*") &&
                     !IsPunct(src, tokens[scan - 1], L"/") &&
                     !IsPunct(src, tokens[scan - 1], L"&&") &&
                     !IsPunct(src, tokens[scan - 1], L"||") &&
                     !IsPunct(src, tokens[scan - 1], L"??") &&
                     !IsPunct(src, tokens[scan - 1], L".") &&
                     !IsPunct(src, tokens[scan - 1], L"?")) {
            break;
          }
        }
        if (scan <= initializerStart) {
          member = initializerStart;
          continue;
        }
        initializerEnd = scan;
        if (scan < bodyClose && IsPunct(src, tokens[scan], L";")) {
          field.end = tokens[scan].end;
          member = scan + 1;
        } else {
          field.end = tokens[scan - 1].end;
          member = scan;
        }
        field.initializer =
            src.substr(tokens[initializerStart].begin,
                       tokens[initializerEnd - 1].end -
                           tokens[initializerStart].begin);
      } else {
        field.end = tokens[member - 1].end;
        if (member < bodyClose && IsPunct(src, tokens[member], L";")) {
          field.end = tokens[member].end;
          ++member;
        }
      }
      fields.push_back(field);
    }

    if (fields.empty() && staticBlocks.empty()) {
      bool hasPrivate = false;
      for (size_t scan = bodyOpen + 1; scan < bodyClose; ++scan) {
        if (IsPrivateName(src, tokens[scan])) {
          hasPrivate = true;
          break;
        }
      }
      if (!hasPrivate) {
        ++index;
        continue;
      }
    }

    const wstring reference =
        hasName ? className : L"__em_class_" + std::to_wstring(sequence);
    wstring instanceFields;
    wstring staticFields;
    for (auto const& field : fields) {
      if (field.isStatic) {
        if (field.initializer.empty()) {
          staticFields +=
              reference + field.accessor + L" = void 0;";
        } else {
          staticFields +=
              reference + field.accessor + L" = (function(){return " +
              RenamePrivateText(field.initializer, sequence) + L";}).call(" +
              reference + L");";
        }
      } else {
        instanceFields +=
            L"this" + field.accessor + L" = " +
            (field.initializer.empty()
                 ? L"void 0"
                 : RenamePrivateText(field.initializer, sequence)) +
            L";";
      }
    }
    for (auto const& block : staticBlocks) {
      staticFields += L"(function(){" + RenamePrivateText(block, sequence) +
                      L"}).call(" + reference + L");";
    }

    vector<std::pair<size_t, size_t>> nested;
    for (size_t scan = bodyOpen + 1; scan < bodyClose; ++scan) {
      if (tokens[scan].kind == TK::Ident &&
          Text(src, tokens[scan]) == L"class" &&
          !(scan > 0 && IsPunct(src, tokens[scan - 1], L"."))) {
        size_t open = scan + 1;
        while (open < bodyClose && !IsPunct(src, tokens[open], L"{")) {
          ++open;
        }
        if (open < bodyClose) {
          const size_t close = FindMatchingForward(src, tokens, open);
          nested.push_back({open, close});
        }
      }
    }
    auto inNested = [&](size_t tokenIndex) {
      for (auto const& range : nested) {
        if (tokenIndex > range.first && tokenIndex < range.second) {
          return true;
        }
      }
      return false;
    };
    auto inField = [&](size_t offset) {
      for (auto const& field : fields) {
        if (offset >= field.begin && offset < field.end) {
          return true;
        }
      }
      for (auto const& range : blockRemovals) {
        if (offset >= range.first && offset < range.second) {
          return true;
        }
      }
      return false;
    };

    vector<Edit> local;
    for (auto const& field : fields) {
      local.push_back({field.begin, field.end, L""});
    }
    for (auto const& range : blockRemovals) {
      local.push_back({range.first, range.second, L""});
    }
    for (size_t scan = bodyOpen + 1; scan < bodyClose; ++scan) {
      if (!IsPrivateName(src, tokens[scan]) || inNested(scan) ||
          inField(tokens[scan].begin)) {
        continue;
      }
      const wstring mangled = PrivateName(src, sequence, Text(src, tokens[scan]));
      if (scan + 1 < bodyClose && tokens[scan + 1].kind == TK::Ident &&
          Text(src, tokens[scan + 1]) == L"in" &&
          !(scan > 0 && IsPunct(src, tokens[scan - 1], L"."))) {
        const size_t end = FindAssignRightEnd(src, tokens, scan + 1);
        if (end > scan + 2) {
          const wstring object =
              src.substr(tokens[scan + 2].begin,
                         tokens[end - 1].end - tokens[scan + 2].begin);
          local.push_back(
              {tokens[scan].begin, tokens[end - 1].end,
               wstring(kPrivateInHelper) + L"(" +
                   RenamePrivateText(object, sequence) + L", \"" + mangled +
                   L"\")"});
          usage.privateIn = true;
          continue;
        }
      }
      local.push_back({tokens[scan].begin, tokens[scan].end, mangled});
    }
    if (!instanceFields.empty()) {
      if (constructorOpen != wstring::npos) {
        size_t insert = tokens[constructorOpen].end;
        if (derived) {
          for (size_t scan = constructorOpen + 1; scan < constructorClose;
               ++scan) {
            if (tokens[scan].kind == TK::Ident &&
                Text(src, tokens[scan]) == L"super") {
              size_t paren = scan + 1;
              while (paren < constructorClose &&
                     !IsPunct(src, tokens[paren], L"(")) {
                ++paren;
              }
              if (paren < constructorClose) {
                const size_t close = FindMatchingForward(src, tokens, paren);
                insert = tokens[close].end;
                if (close + 1 < constructorClose &&
                    IsPunct(src, tokens[close + 1], L";")) {
                  insert = tokens[close + 1].end;
                }
              }
              break;
            }
          }
        }
        local.push_back({insert, insert, instanceFields});
      } else {
        wstring constructorText =
            derived ? L"constructor(...args){super(...args);"
                    : L"constructor(){";
        constructorText += instanceFields + L"}";
        local.push_back(
            {tokens[bodyOpen].end, tokens[bodyOpen].end, constructorText});
      }
    }

    bool declaration = ClassIsDeclaration(src, tokens, index);
    if (!hasName && index > 0 && tokens[index - 1].kind == TK::Ident &&
        Text(src, tokens[index - 1]) == L"default") {
      declaration = false;
    }
    if (!declaration) {
      const size_t classBegin = tokens[index].begin;
      const size_t classEnd = tokens[bodyClose].end;
      wstring classText = src.substr(classBegin, classEnd - classBegin);
      vector<Edit> relative;
      for (auto const& edit : local) {
        relative.push_back({edit.begin - classBegin, edit.end - classBegin,
                            edit.text});
      }
      classText = ApplyEdits(classText, relative);
      wstring wrapper = L"(()=>{let " + reference + L" = " + classText + L";";
      if (!staticFields.empty()) {
        wrapper += staticFields;
      }
      wrapper += L"return " + reference + L";})()";
      edits.push_back({classBegin, classEnd, wrapper});
      changed = true;
    } else {
      for (auto const& edit : local) {
        edits.push_back(edit);
      }
      if (!staticFields.empty()) {
        edits.push_back(
            {tokens[bodyClose].end, tokens[bodyClose].end, L";" + staticFields});
      }
      changed = true;
    }
    index = bodyClose + 1;
  }
  return ApplyEdits(src, edits);
}

wstring ReplaceTemplates(wstring const& src, Usage& usage, bool& changed) {
  vector<Tok> tokens = Tokenize(src);
  vector<Edit> edits;
  for (auto const& token : tokens) {
    if (token.kind != TK::Template) {
      continue;
    }
    size_t index = token.begin + 1;
    const size_t limit = token.end > 0 ? token.end - 1 : 0;
    while (index < limit) {
      const wchar_t ch = src[index];
      if (ch == L'\\') {
        index += 2;
        continue;
      }
      if (ch == L'$' && index + 1 < limit && src[index + 1] == L'{') {
        const size_t after = ScanTemplateExpression(src, index + 2);
        const size_t expressionEnd = after > 0 ? after - 1 : index + 2;
        if (expressionEnd > index + 2) {
          const wstring inner =
              src.substr(index + 2, expressionEnd - (index + 2));
          const wstring transformed = TranspileInner(inner, usage);
          if (transformed != inner) {
            edits.push_back({index + 2, expressionEnd, transformed});
            changed = true;
          }
        }
        index = after;
        continue;
      }
      ++index;
    }
  }
  return ApplyEdits(src, edits);
}

wstring InjectHelpers(wstring const& src, Usage const& usage) {
  wstring helpers;
  if (usage.optionalChain) {
    helpers += wstring(L"var ") + kOcHelper +
               L"=function(v,f){return v===null||v===void 0?void 0:f(v);};";
  }
  if (usage.nullish) {
    helpers += wstring(L"var ") + kNullishHelper +
               L"=function(v,f){return v===null||v===void 0?f():v;};";
  }
  if (usage.spread) {
    helpers += wstring(L"var ") + kSpreadHelper +
               L"=function(t){var d=function(o,p,v){Object.defineProperty(o,p,"
               L"{value:v,enumerable:true,writable:true,configurable:true});};"
               L"for(var i=1;i<arguments.length;i++){var s=arguments[i];"
               L"if(s!=null){var ks=Object.keys(s);for(var j=0;j<ks.length;j++)"
               L"d(t,ks[j],s[ks[j]]);if(Object.getOwnPropertySymbols){var sy="
               L"Object.getOwnPropertySymbols(s);for(var m=0;m<sy.length;m++){"
               L"var y=sy[m];if(Object.prototype.propertyIsEnumerable.call(s,y))"
               L"d(t,y,s[y]);}}}}return t;};";
  }
  if (usage.rest) {
    helpers += wstring(L"var ") + kRestHelper +
               L"=function(o,k){var t={};if(o!=null){var n=[];for(var j=0;"
               L"j<k.length;j++)n.push(typeof k[j]===\"symbol\"?k[j]:"
               L"String(k[j]));var d=function(p,v){Object.defineProperty(t,p,"
               L"{value:v,enumerable:true,writable:true,configurable:true});};"
               L"var ks=Object.keys(o);for(var i=0;i<ks.length;i++){var p="
               L"ks[i];if(n.indexOf(p)<0)d(p,o[p]);}if(Object."
               L"getOwnPropertySymbols){var sy=Object.getOwnPropertySymbols(o);"
               L"for(var m=0;m<sy.length;m++){var y=sy[m];if(n.indexOf(y)<0&&"
               L"Object.prototype.propertyIsEnumerable.call(o,y))d(y,o[y]);}}}"
               L"return t;};";
  }
  if (usage.privateIn) {
    helpers += wstring(L"var ") + kPrivateInHelper +
               L"=function(o,p){return o!=null&&Object.prototype.hasOwnProperty."
               L"call(o,p);};";
  }
  if (helpers.empty()) {
    return src;
  }

  vector<Tok> tokens = Tokenize(src);
  const size_t insert = TopLevelDirectiveInsert(src, tokens);
  wstring output = src.substr(0, insert);
  if (insert != 0) {
    output += L"\n";
  }
  output += helpers;
  output += L"\n";
  output += src.substr(insert);
  return output;
}

wstring TranspileInner(wstring const& source, Usage& usage) {
  wstring output = source;
  for (int pass = 0; pass < 20; ++pass) {
    bool changed = false;
    wstring next = ReplaceParamRest(output, usage, changed);
    next = ReplaceClassFields(next, usage, changed);
    next = ReplaceForOfRest(next, usage, changed);
    next = ReplaceObjectRest(next, usage, changed);
    next = ReplaceRestAssignments(next, usage, changed);
    next = ReplaceObjectSpread(next, usage, changed);
    next = ReplaceOptional(next, usage, changed);
    next = ReplaceNullish(next, usage, changed);
    next = ReplaceLogicalAssign(next, usage, changed);
    next = ReplaceNumbers(next, usage, changed);
    next = ReplaceTemplates(next, usage, changed);
    if (!changed || next == output) {
      output = next;
      break;
    }
    output = next;
  }
  return output;
}

}  // namespace

bool HasClassField(wstring const& src, vector<Tok> const& tokens,
                   size_t classIndex) {
  size_t cursor = classIndex + 1;
  if (cursor < tokens.size() && tokens[cursor].kind == TK::Ident &&
      Text(src, tokens[cursor]) != L"extends") {
    ++cursor;
  }
  int depth = 0;
  size_t bodyOpen = wstring::npos;
  for (; cursor < tokens.size(); ++cursor) {
    Tok const& token = tokens[cursor];
    if (IsOpen(src, token)) {
      if (depth == 0 && IsPunct(src, token, L"{")) {
        bodyOpen = cursor;
        break;
      }
      ++depth;
    } else if (IsClose(src, token)) {
      if (depth > 0) {
        --depth;
      }
    }
  }
  if (bodyOpen == wstring::npos) {
    return false;
  }
  const size_t bodyClose = FindMatchingForward(src, tokens, bodyOpen);
  depth = 0;
  for (size_t index = bodyOpen + 1; index < bodyClose; ++index) {
    if (depth == 0 && index > bodyOpen + 1 &&
        IsPunct(src, tokens[index], L"{") &&
        IsIdent(src, tokens[index - 1], L"static")) {
      return true;
    }
    if (IsOpen(src, tokens[index])) {
      ++depth;
    } else if (IsClose(src, tokens[index])) {
      if (depth > 0) {
        --depth;
      }
    } else if (depth == 0 && index > bodyOpen + 1 &&
               (IsPunct(src, tokens[index], L"=") ||
                IsPunct(src, tokens[index], L";"))) {
      Tok const& previous = tokens[index - 1];
      if (previous.kind == TK::Ident || previous.kind == TK::String ||
          previous.kind == TK::Number ||
          IsPunct(src, previous, L"]")) {
        return true;
      }
    }
  }
  return false;
}

bool LooksLikeModernScript(wstring const& source) {
  vector<Tok> tokens = Tokenize(source);
  for (size_t index = 0; index < tokens.size(); ++index) {
    Tok const& token = tokens[index];
    if (IsPunct(source, token, L"?.") || IsPunct(source, token, L"??") ||
        IsPunct(source, token, L"??=") || IsPunct(source, token, L"||=") ||
        IsPunct(source, token, L"&&=") || IsPunct(source, token, L"...")) {
      return true;
    }
    if (token.kind == TK::Number &&
        Text(source, token).find(L'_') != wstring::npos) {
      return true;
    }
    if (token.kind == TK::Template) {
      const wstring text = Text(source, token);
      if (text.find(L"?.") != wstring::npos ||
          text.find(L"??") != wstring::npos ||
          text.find(L"...") != wstring::npos ||
          text.find(L"||=") != wstring::npos ||
          text.find(L"&&=") != wstring::npos ||
          text.find(L"#") != wstring::npos ||
          (text.find(L"class") != wstring::npos &&
           text.find(L"=") != wstring::npos)) {
        return true;
      }
      for (size_t scan = 1; scan + 1 < text.size(); ++scan) {
        if (text[scan] == L'_' && std::iswdigit(text[scan - 1]) &&
            std::iswdigit(text[scan + 1])) {
          return true;
        }
      }
    }
    if (IsPrivateName(source, token)) {
      return true;
    }
    if (IsIdent(source, token, L"class") &&
        !(index > 0 && IsPunct(source, tokens[index - 1], L".")) &&
        HasClassField(source, tokens, index)) {
      return true;
    }
  }
  return false;
}

wstring TranspileJavaScript(wstring const& source) {
  Usage usage;
  return InjectHelpers(TranspileInner(source, usage), usage);
}

}  // namespace edgeminus::content
