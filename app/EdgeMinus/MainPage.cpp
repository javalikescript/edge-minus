#include "pch.h"

#include <urlmon.h>

#include <chrono>

#include "ContentAdapter.h"
#include "Extensions.h"
#include "MainPage.h"

#include "Log.h"

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Foundation::Metadata;
using namespace winrt::Windows::Management::Deployment;
using namespace winrt::Windows::System;
using namespace winrt::Windows::UI;
using namespace winrt::Windows::UI::Core;
using namespace winrt::Windows::UI::Text;
using namespace winrt::Windows::UI::ViewManagement;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Windows::UI::Xaml::Input;
using namespace winrt::Windows::UI::Xaml::Media;
using namespace winrt::Windows::Web;

namespace edgeminus {
namespace {

Color ChromeBg() { return ColorHelper::FromArgb(255, 28, 27, 34); }
Color ChromeFg() { return ColorHelper::FromArgb(255, 245, 245, 248); }
Color MutedFg() { return ColorHelper::FromArgb(255, 176, 176, 186); }
Color Accent() { return ColorHelper::FromArgb(255, 224, 108, 31); }
Color ButtonBg() { return ColorHelper::FromArgb(255, 52, 52, 60); }
Color ContentBg() { return ColorHelper::FromArgb(255, 255, 255, 255); }
Color ErrorBg() { return ColorHelper::FromArgb(255, 23, 22, 28); }

SolidColorBrush Brush(Color color) {
  SolidColorBrush brush;
  brush.Color(color);
  return brush;
}

FontIcon MakeGlyphIcon(std::wstring_view glyph) {
  FontIcon icon;
  icon.FontFamily(FontFamily(L"Segoe MDL2 Assets"));
  icon.Glyph(winrt::hstring(glyph));
  return icon;
}

Button MakeGlyphButton(std::wstring_view glyph) {
  auto icon = MakeGlyphIcon(glyph);
  icon.FontSize(17);
  icon.Foreground(Brush(ChromeFg()));

  Button button;
  button.Content(icon);
  button.Background(Brush(ButtonBg()));
  button.BorderThickness(ThicknessHelper::FromUniformLength(0));
  button.Padding(ThicknessHelper::FromLengths(4, 4, 4, 4));
  button.Margin(ThicknessHelper::FromLengths(1, 0, 1, 0));
  button.MinWidth(34);
  button.MinHeight(34);
  return button;
}

std::wstring Trim(std::wstring text) {
  const size_t first = text.find_first_not_of(L" \t\r\n");
  if (first == std::wstring::npos) {
    return {};
  }
  const size_t last = text.find_last_not_of(L" \t\r\n");
  return text.substr(first, last - first + 1);
}

bool StartsWith(std::wstring const& text, std::wstring_view prefix) {
  return text.size() >= prefix.size() &&
         _wcsnicmp(text.c_str(), prefix.data(), prefix.size()) == 0;
}

std::wstring ExtensionForMediaType(std::wstring mediaType) {
  std::transform(mediaType.begin(), mediaType.end(), mediaType.begin(),
                 [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
  const size_t semicolon = mediaType.find(L';');
  if (semicolon != std::wstring::npos) {
    mediaType = mediaType.substr(0, semicolon);
  }
  mediaType = Trim(mediaType);
  if (mediaType == L"application/pdf") return L".pdf";
  if (mediaType == L"application/zip" ||
      mediaType == L"application/x-zip-compressed") {
    return L".zip";
  }
  if (mediaType == L"application/x-7z-compressed") return L".7z";
  if (mediaType == L"application/x-rar-compressed") return L".rar";
  if (mediaType == L"application/vnd.android.package-archive") return L".apk";
  if (mediaType == L"application/x-msdownload" ||
      mediaType == L"application/x-msdos-program") {
    return L".exe";
  }
  if (mediaType == L"application/x-msi" ||
      mediaType == L"application/x-ms-installer") {
    return L".msi";
  }
  if (mediaType == L"application/msword") return L".doc";
  if (mediaType ==
      L"application/vnd.openxmlformats-officedocument.wordprocessingml.document") {
    return L".docx";
  }
  if (mediaType == L"application/vnd.ms-excel") return L".xls";
  if (mediaType ==
      L"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet") {
    return L".xlsx";
  }
  if (mediaType == L"application/vnd.ms-powerpoint") return L".ppt";
  if (mediaType ==
      L"application/vnd.openxmlformats-officedocument.presentationml.presentation") {
    return L".pptx";
  }
  if (mediaType == L"audio/mpeg") return L".mp3";
  if (mediaType == L"audio/ogg") return L".ogg";
  if (mediaType == L"audio/wav" || mediaType == L"audio/x-wav") return L".wav";
  if (mediaType == L"video/mp4") return L".mp4";
  if (mediaType == L"image/jpeg") return L".jpg";
  if (mediaType == L"image/png") return L".png";
  if (mediaType == L"image/gif") return L".gif";
  if (mediaType == L"text/plain") return L".txt";
  if (mediaType == L"application/json") return L".json";
  return {};
}

std::wstring FileNameFromUri(Uri const& uri, std::wstring const& mediaType) {
  const std::wstring path(uri.Path());
  const size_t slash = path.find_last_of(L"/\\");
  std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
  if (!name.empty()) {
    try {
      name = std::wstring(Uri::UnescapeComponent(winrt::hstring(name)));
    } catch (...) {
    }
  }

  const std::wstring invalid = L"<>:\"/\\|?*";
  std::wstring sanitized;
  sanitized.reserve(name.size());
  for (wchar_t ch : name) {
    if (ch < 0x20 || invalid.find(ch) != std::wstring::npos) {
      sanitized += L'_';
    } else {
      sanitized += ch;
    }
  }
  while (!sanitized.empty() &&
         (sanitized.back() == L'.' || sanitized.back() == L' ')) {
    sanitized.pop_back();
  }
  if (sanitized.empty()) {
    sanitized = L"download";
  }

  if (sanitized.find(L'.') == std::wstring::npos) {
    const winrt::hstring extension = uri.Extension();
    if (!extension.empty()) {
      sanitized += extension.c_str();
    } else {
      sanitized += ExtensionForMediaType(mediaType);
    }
  }
  return sanitized;
}

std::wstring PermissionName(WebViewPermissionType type) {
  switch (type) {
    case WebViewPermissionType::Geolocation:
      return L"your location";
    case WebViewPermissionType::Media:
      return L"your camera or microphone";
    case WebViewPermissionType::PointerLock:
      return L"your mouse pointer";
    case WebViewPermissionType::UnlimitedIndexedDBQuota:
      return L"unlimited local storage";
    case WebViewPermissionType::WebNotifications:
      return L"notifications";
    default:
      return L"a browser feature";
  }
}

constexpr wchar_t kSearchUrlKey[] = L"searchUrl";
constexpr wchar_t kUserAgentKey[] = L"userAgent";
constexpr wchar_t kHomeUrlKey[] = L"homeUrl";
constexpr wchar_t kUpdateUrlKey[] = L"updateUrl";
constexpr wchar_t kDefaultSearchUrl[] = L"https://www.bing.com/search?q={query}";
constexpr wchar_t kDefaultHomeUrl[] = L"about:blank";
constexpr wchar_t kDefaulUserAgent[] = L"Mozilla/5.0 (Android 10.0; Mobile; rv:84.0) Gecko/20100101 Firefox/88.0";
constexpr double kLongScriptThresholdMs = 5000.0;

std::wstring LoadSetting(std::wstring_view key) {
  try {
    auto values = winrt::Windows::Storage::ApplicationData::Current()
                      .LocalSettings()
                      .Values();
    auto value = values.TryLookup(winrt::hstring(key));
    if (value) {
      return std::wstring(winrt::unbox_value<winrt::hstring>(value).c_str());
    }
  } catch (...) {
  }
  return {};
}

void StoreSetting(std::wstring_view key, std::wstring const& value) {
  try {
    auto values = winrt::Windows::Storage::ApplicationData::Current()
                      .LocalSettings()
                      .Values();
    if (value.empty()) {
      values.Remove(winrt::hstring(key));
    } else {
      values.Insert(winrt::hstring(key),
                    winrt::box_value(winrt::hstring(value)));
    }
  } catch (...) {
  }
}

void SetCheckBoxChecked(CheckBox const& box, bool checked) {
  box.IsChecked(
      winrt::box_value(checked)
          .as<winrt::Windows::Foundation::IReference<bool>>());
}

bool IsCheckBoxChecked(CheckBox const& box) {
  const auto value = box.IsChecked();
  return value ? value.Value() : false;
}

// Set the process-wide default user agent used by the WebView engine.
// Mirrors UrlMkSetSessionOption(URLMON_OPTION_USERAGENT) as used by WUT so the
// value also applies to links, redirects and script navigations, not just the
// first request.
constexpr DWORD kUrlMonOptionUserAgent = 0x10000001;
constexpr DWORD kUrlMonOptionUserAgentRefresh = 0x10000002;

void SetDefaultUserAgent(std::wstring const& userAgent) {
  UrlMkSetSessionOption(kUrlMonOptionUserAgentRefresh, nullptr, 0, 0);
  if (userAgent.empty()) {
    log::Write(L"main: user agent reset to default");
    return;
  }

  const int wideLength = static_cast<int>(userAgent.size());
  const int byteLength = WideCharToMultiByte(CP_ACP, 0, userAgent.c_str(),
                                             wideLength, nullptr, 0, nullptr,
                                             nullptr);
  if (byteLength <= 0) {
    return;
  }

  std::string narrow(static_cast<size_t>(byteLength), '\0');
  WideCharToMultiByte(CP_ACP, 0, userAgent.c_str(), wideLength, &narrow[0],
                      byteLength, nullptr, nullptr);
  const HRESULT hr =
      UrlMkSetSessionOption(kUrlMonOptionUserAgent, narrow.data(),
                            static_cast<DWORD>(narrow.size()), 0);
  log::Write(L"main: user agent set hr=" +
             std::to_wstring(static_cast<long>(hr)));
}

constexpr uint32_t kMaxFaviconBytes = 2u * 1024u * 1024u;

winrt::Windows::Foundation::IAsyncAction SaveScaledPngAsync(
    winrt::Windows::Storage::Streams::InMemoryRandomAccessStream source,
    uint32_t size, winrt::Windows::Storage::StorageFile file) {
  using namespace winrt::Windows::Graphics::Imaging;
  using namespace winrt::Windows::Storage;

  source.Seek(0);
  auto decoder = co_await BitmapDecoder::CreateAsync(source);

  uint32_t bestFrame = 0;
  uint64_t bestArea = 0;
  for (uint32_t index = 0; index < decoder.FrameCount(); ++index) {
    auto frame = co_await decoder.GetFrameAsync(index);
    const uint64_t area = static_cast<uint64_t>(frame.PixelWidth()) * frame.PixelHeight();
    if (area > bestArea) {
      bestArea = area;
      bestFrame = index;
    }
  }

  BitmapTransform transform;
  transform.ScaledWidth(size);
  transform.ScaledHeight(size);
  transform.InterpolationMode(BitmapInterpolationMode::Fant);

  auto frame = co_await decoder.GetFrameAsync(bestFrame);
  auto bitmap = co_await frame.GetSoftwareBitmapAsync(
      BitmapPixelFormat::Bgra8, BitmapAlphaMode::Premultiplied, transform,
      ExifOrientationMode::IgnoreExifOrientation,
      ColorManagementMode::ColorManageToSRgb);

  auto stream = co_await file.OpenAsync(FileAccessMode::ReadWrite);
  auto encoder = co_await BitmapEncoder::CreateAsync(BitmapEncoder::PngEncoderId(), stream);
  encoder.SetSoftwareBitmap(bitmap);
  co_await encoder.FlushAsync();
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

}  // namespace

MainPage::MainPage(winrt::hstring const& initialUrl) {
  log::Write(L"main: building UI");
  BuildUi();
  WireEvents();
  WireSystemBack();
  ApplySystemChrome();
  SetDefaultUserAgent(std::wstring(UserAgent()));
  log::Write(L"main: opening start page");
  NavigateTo(initialUrl.empty() ? HomeUrl() : initialUrl);
}

void MainPage::BuildUi() {
  root_ = Grid();
  root_.Background(Brush(ChromeBg()));

  auto topRow = RowDefinition();
  topRow.Height(GridLengthHelper::Auto());
  auto contentRow = RowDefinition();
  contentRow.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  auto statusRow = RowDefinition();
  statusRow.Height(GridLengthHelper::Auto());
  root_.RowDefinitions().Append(topRow);
  root_.RowDefinitions().Append(contentRow);
  root_.RowDefinitions().Append(statusRow);

  progress_ = ProgressBar();
  progress_.IsIndeterminate(true);
  progress_.Minimum(0);
  progress_.Maximum(1);
  progress_.Height(3);
  progress_.Foreground(Brush(Accent()));
  progress_.Visibility(Visibility::Collapsed);

  auto buttons = Grid();
  for (int i = 0; i < 4; ++i) {
    ColumnDefinition column;
    column.Width(i == 1 ? GridLengthHelper::FromValueAndType(1, GridUnitType::Star)
                        : GridLengthHelper::Auto());
    buttons.ColumnDefinitions().Append(column);
  }

  reloadButton_ = MakeGlyphButton(L"\uE72C");
  goButton_ = MakeGlyphButton(L"\uE721");
  menuButton_ = MakeGlyphButton(L"\uE712");

  addressBar_ = TextBox();
  addressBar_.PlaceholderText(L"Search or enter address");
  addressBar_.FontSize(14);
  addressBar_.MinWidth(90);
  addressBar_.Margin(ThicknessHelper::FromLengths(4, 2, 4, 2));
  addressBar_.Padding(ThicknessHelper::FromLengths(8, 4, 8, 4));
  addressBar_.VerticalAlignment(VerticalAlignment::Center);
  addressBar_.Background(Brush(ButtonBg()));
  addressBar_.Foreground(Brush(ChromeFg()));
  addressBar_.BorderBrush(Brush(Accent()));
  addressBar_.BorderThickness(ThicknessHelper::FromUniformLength(1));

  InputScope scope;
  InputScopeName name;
  name.NameValue(InputScopeNameValue::Url);
  scope.Names().Append(name);
  addressBar_.InputScope(scope);

  Grid::SetColumn(reloadButton_, 0);
  Grid::SetColumn(addressBar_, 1);
  Grid::SetColumn(goButton_, 2);
  Grid::SetColumn(menuButton_, 3);

  buttons.Children().Append(reloadButton_);
  buttons.Children().Append(addressBar_);
  buttons.Children().Append(goButton_);
  buttons.Children().Append(menuButton_);

  auto chrome = StackPanel();
  chrome.Background(Brush(ChromeBg()));
  chrome.Children().Append(progress_);
  chrome.Children().Append(buttons);
  Grid::SetRow(chrome, 0);
  root_.Children().Append(chrome);

  contentHost_ = Grid();
  contentHost_.Background(Brush(ContentBg()));

  web_ = WebView();
  web_.HorizontalAlignment(HorizontalAlignment::Stretch);
  web_.VerticalAlignment(VerticalAlignment::Stretch);
  web_.Settings().IsJavaScriptEnabled(true);
  web_.Settings().IsIndexedDBEnabled(true);
  contentHost_.Children().Append(web_);

  errorPanel_ = Border();
  errorPanel_.Background(Brush(ErrorBg()));
  errorPanel_.Visibility(Visibility::Collapsed);

  auto errorStack = StackPanel();
  errorStack.HorizontalAlignment(HorizontalAlignment::Center);
  errorStack.VerticalAlignment(VerticalAlignment::Center);
  errorStack.MaxWidth(420);

  errorTitle_ = TextBlock();
  errorTitle_.FontSize(20);
  errorTitle_.FontWeight(FontWeights::Bold());
  errorTitle_.Foreground(Brush(ChromeFg()));
  errorTitle_.TextAlignment(TextAlignment::Center);
  errorTitle_.TextWrapping(TextWrapping::Wrap);
  errorTitle_.Margin(ThicknessHelper::FromLengths(24, 0, 24, 0));

  errorText_ = TextBlock();
  errorText_.FontSize(14);
  errorText_.Foreground(Brush(MutedFg()));
  errorText_.TextAlignment(TextAlignment::Center);
  errorText_.TextWrapping(TextWrapping::Wrap);
  errorText_.Margin(ThicknessHelper::FromLengths(24, 10, 24, 18));

  retryButton_ = Button();
  retryButton_.Content(winrt::box_value(winrt::hstring(L"Try again")));
  retryButton_.HorizontalAlignment(HorizontalAlignment::Center);
  retryButton_.Background(Brush(Accent()));
  retryButton_.Foreground(Brush(ChromeFg()));
  retryButton_.BorderThickness(ThicknessHelper::FromUniformLength(0));
  retryButton_.Padding(ThicknessHelper::FromLengths(18, 8, 18, 8));

  errorStack.Children().Append(errorTitle_);
  errorStack.Children().Append(errorText_);
  errorStack.Children().Append(retryButton_);
  errorPanel_.Child(errorStack);
  contentHost_.Children().Append(errorPanel_);

  BuildSettingsPanel();
  BuildExtensionsPanel();
  BuildLogsPanel();

  Grid::SetRow(contentHost_, 1);
  root_.Children().Append(contentHost_);

  statusText_ = TextBlock();
  statusText_.FontSize(11);
  statusText_.Foreground(Brush(MutedFg()));
  statusText_.TextTrimming(TextTrimming::CharacterEllipsis);
  statusText_.Margin(ThicknessHelper::FromLengths(10, 3, 10, 3));
  statusText_.Visibility(Visibility::Collapsed);
  Grid::SetRow(statusText_, 2);
  root_.Children().Append(statusText_);

  UpdateChrome();
}

void MainPage::BuildSettingsPanel() {
  settingsPanel_ = Border();
  settingsPanel_.Background(Brush(ErrorBg()));
  settingsPanel_.Visibility(Visibility::Collapsed);

  auto stack = StackPanel();
  stack.HorizontalAlignment(HorizontalAlignment::Center);
  stack.VerticalAlignment(VerticalAlignment::Center);
  stack.MaxWidth(440);
  stack.Margin(ThicknessHelper::FromLengths(20, 20, 20, 20));

  auto title = TextBlock();
  title.Text(L"Settings");
  title.FontSize(24);
  title.FontWeight(FontWeights::Bold());
  title.Foreground(Brush(ChromeFg()));
  title.Margin(ThicknessHelper::FromLengths(0, 0, 0, 16));
  stack.Children().Append(title);

  auto searchLabel = TextBlock();
  searchLabel.Text(L"Search URL");
  searchLabel.FontSize(14);
  searchLabel.Foreground(Brush(ChromeFg()));
  searchLabel.Margin(ThicknessHelper::FromLengths(0, 0, 0, 4));
  stack.Children().Append(searchLabel);

  searchBox_ = TextBox();
  searchBox_.FontSize(14);
  searchBox_.Padding(ThicknessHelper::FromLengths(8, 6, 8, 6));
  searchBox_.Background(Brush(ButtonBg()));
  searchBox_.Foreground(Brush(ChromeFg()));
  searchBox_.BorderBrush(Brush(Accent()));
  searchBox_.BorderThickness(ThicknessHelper::FromUniformLength(1));
  stack.Children().Append(searchBox_);

  auto searchHint = TextBlock();
  searchHint.Text(L"Used for text that is not a URL. {query} is replaced with the terms.");
  searchHint.FontSize(11);
  searchHint.Foreground(Brush(MutedFg()));
  searchHint.TextWrapping(TextWrapping::Wrap);
  searchHint.Margin(ThicknessHelper::FromLengths(0, 4, 0, 16));
  stack.Children().Append(searchHint);

  auto homeLabel = TextBlock();
  homeLabel.Text(L"Home page");
  homeLabel.FontSize(14);
  homeLabel.Foreground(Brush(ChromeFg()));
  homeLabel.Margin(ThicknessHelper::FromLengths(0, 0, 0, 4));
  stack.Children().Append(homeLabel);

  homeBox_ = TextBox();
  homeBox_.FontSize(14);
  homeBox_.PlaceholderText(L"about:blank");
  homeBox_.Padding(ThicknessHelper::FromLengths(8, 6, 8, 6));
  homeBox_.Background(Brush(ButtonBg()));
  homeBox_.Foreground(Brush(ChromeFg()));
  homeBox_.BorderBrush(Brush(Accent()));
  homeBox_.BorderThickness(ThicknessHelper::FromUniformLength(1));
  stack.Children().Append(homeBox_);

  auto homeHint = TextBlock();
  homeHint.Text(L"Opened by the home button and on startup. Defaults to about:blank.");
  homeHint.FontSize(11);
  homeHint.Foreground(Brush(MutedFg()));
  homeHint.TextWrapping(TextWrapping::Wrap);
  homeHint.Margin(ThicknessHelper::FromLengths(0, 4, 0, 16));
  stack.Children().Append(homeHint);

  auto uaLabel = TextBlock();
  uaLabel.Text(L"User agent");
  uaLabel.FontSize(14);
  uaLabel.Foreground(Brush(ChromeFg()));
  uaLabel.Margin(ThicknessHelper::FromLengths(0, 0, 0, 4));
  stack.Children().Append(uaLabel);

  userAgentBox_ = TextBox();
  userAgentBox_.FontSize(14);
  userAgentBox_.PlaceholderText(L"Default");
  userAgentBox_.Padding(ThicknessHelper::FromLengths(8, 6, 8, 6));
  userAgentBox_.Background(Brush(ButtonBg()));
  userAgentBox_.Foreground(Brush(ChromeFg()));
  userAgentBox_.BorderBrush(Brush(Accent()));
  userAgentBox_.BorderThickness(ThicknessHelper::FromUniformLength(1));
  stack.Children().Append(userAgentBox_);

  auto uaHint = TextBlock();
  uaHint.Text(L"Leave empty to use the system user agent.");
  uaHint.FontSize(11);
  uaHint.Foreground(Brush(MutedFg()));
  uaHint.TextWrapping(TextWrapping::Wrap);
  uaHint.Margin(ThicknessHelper::FromLengths(0, 4, 0, 20));
  stack.Children().Append(uaHint);

  auto updateLabel = TextBlock();
  updateLabel.Text(L"Update URL");
  updateLabel.FontSize(14);
  updateLabel.Foreground(Brush(ChromeFg()));
  updateLabel.Margin(ThicknessHelper::FromLengths(0, 0, 0, 4));
  stack.Children().Append(updateLabel);

  updateUrlBox_ = TextBox();
  updateUrlBox_.FontSize(14);
  updateUrlBox_.PlaceholderText(L"https://example.com/EdgeMinus.appinstaller");
  updateUrlBox_.Padding(ThicknessHelper::FromLengths(8, 6, 8, 6));
  updateUrlBox_.Background(Brush(ButtonBg()));
  updateUrlBox_.Foreground(Brush(ChromeFg()));
  updateUrlBox_.BorderBrush(Brush(Accent()));
  updateUrlBox_.BorderThickness(ThicknessHelper::FromUniformLength(1));
  stack.Children().Append(updateUrlBox_);

  auto updateHint = TextBlock();
  updateHint.Text(L"An .appinstaller URL used by Check for updates.");
  updateHint.FontSize(11);
  updateHint.Foreground(Brush(MutedFg()));
  updateHint.TextWrapping(TextWrapping::Wrap);
  updateHint.Margin(ThicknessHelper::FromLengths(0, 4, 0, 20));
  stack.Children().Append(updateHint);

  logsCheckBox_ = CheckBox();
  logsCheckBox_.Content(winrt::box_value(winrt::hstring(L"Enable logging")));
  logsCheckBox_.FontSize(14);
  logsCheckBox_.Foreground(Brush(ChromeFg()));
  logsCheckBox_.IsThreeState(false);
  stack.Children().Append(logsCheckBox_);

  auto logsHint = TextBlock();
  logsHint.Text(
      L"Writes diagnostics to a log file in the app's local folder. Disabled "
      L"by default.");
  logsHint.FontSize(11);
  logsHint.Foreground(Brush(MutedFg()));
  logsHint.TextWrapping(TextWrapping::Wrap);
  logsHint.Margin(ThicknessHelper::FromLengths(0, 4, 0, 20));
  stack.Children().Append(logsHint);

  auto tools = StackPanel();
  tools.Orientation(Orientation::Horizontal);
  tools.Margin(ThicknessHelper::FromLengths(0, 0, 0, 20));

  logsButton_ = Button();
  logsButton_.Content(winrt::box_value(winrt::hstring(L"View logs")));
  logsButton_.Background(Brush(ButtonBg()));
  logsButton_.Foreground(Brush(ChromeFg()));
  logsButton_.BorderThickness(ThicknessHelper::FromUniformLength(0));
  logsButton_.Padding(ThicknessHelper::FromLengths(18, 8, 18, 8));
  logsButton_.Margin(ThicknessHelper::FromLengths(0, 0, 8, 0));
  logsButton_.IsEnabled(false);
  logsButton_.Click([this](IInspectable const&, RoutedEventArgs const&) {
    ShowLogs();
  });
  tools.Children().Append(logsButton_);

  auto updateButton = Button();
  updateButton.Content(winrt::box_value(winrt::hstring(L"Check for updates")));
  updateButton.Background(Brush(ButtonBg()));
  updateButton.Foreground(Brush(ChromeFg()));
  updateButton.BorderThickness(ThicknessHelper::FromUniformLength(0));
  updateButton.Padding(ThicknessHelper::FromLengths(18, 8, 18, 8));
  updateButton.Click([this](IInspectable const&, RoutedEventArgs const&) {
    CheckForUpdates(Trim(std::wstring(updateUrlBox_.Text())));
  });
  tools.Children().Append(updateButton);

  stack.Children().Append(tools);

  auto actions = StackPanel();
  actions.Orientation(Orientation::Horizontal);
  actions.HorizontalAlignment(HorizontalAlignment::Right);

  auto cancelButton = Button();
  cancelButton.Content(winrt::box_value(winrt::hstring(L"Cancel")));
  cancelButton.Background(Brush(ButtonBg()));
  cancelButton.Foreground(Brush(ChromeFg()));
  cancelButton.BorderThickness(ThicknessHelper::FromUniformLength(0));
  cancelButton.Padding(ThicknessHelper::FromLengths(18, 8, 18, 8));
  cancelButton.Margin(ThicknessHelper::FromLengths(0, 0, 8, 0));
  cancelButton.Click([this](IInspectable const&, RoutedEventArgs const&) {
    HideSettings();
  });
  actions.Children().Append(cancelButton);

  auto saveButton = Button();
  saveButton.Content(winrt::box_value(winrt::hstring(L"Save")));
  saveButton.Background(Brush(Accent()));
  saveButton.Foreground(Brush(ChromeFg()));
  saveButton.BorderThickness(ThicknessHelper::FromUniformLength(0));
  saveButton.Padding(ThicknessHelper::FromLengths(18, 8, 18, 8));
  saveButton.Click([this](IInspectable const&, RoutedEventArgs const&) {
    SaveSettings();
  });
  actions.Children().Append(saveButton);

  stack.Children().Append(actions);

  auto scroll = ScrollViewer();
  scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
  scroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
  scroll.Content(stack);
  settingsPanel_.Child(scroll);
  contentHost_.Children().Append(settingsPanel_);
}

void MainPage::BuildExtensionsPanel() {
  extensionsPanel_ = Border();
  extensionsPanel_.Background(Brush(ErrorBg()));
  extensionsPanel_.Visibility(Visibility::Collapsed);

  auto layout = Grid();
  layout.Margin(ThicknessHelper::FromLengths(16, 12, 16, 12));

  auto headerRow = RowDefinition();
  headerRow.Height(GridLengthHelper::Auto());
  auto bodyRow = RowDefinition();
  bodyRow.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  auto actionsRow = RowDefinition();
  actionsRow.Height(GridLengthHelper::Auto());
  layout.RowDefinitions().Append(headerRow);
  layout.RowDefinitions().Append(bodyRow);
  layout.RowDefinitions().Append(actionsRow);

  auto header = Grid();
  auto titleColumn = ColumnDefinition();
  titleColumn.Width(
      GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  auto closeColumn = ColumnDefinition();
  closeColumn.Width(GridLengthHelper::Auto());
  header.ColumnDefinitions().Append(titleColumn);
  header.ColumnDefinitions().Append(closeColumn);

  auto title = TextBlock();
  title.Text(L"Extensions");
  title.FontSize(20);
  title.FontWeight(FontWeights::Bold());
  title.Foreground(Brush(ChromeFg()));
  title.VerticalAlignment(VerticalAlignment::Center);
  Grid::SetColumn(title, 0);
  header.Children().Append(title);

  // auto closeIcon = FontIcon();
  // closeIcon.FontFamily(FontFamily(L"Segoe MDL2 Assets"));
  // closeIcon.Glyph(L"\uE8BB");
  auto closeButton = Button();
  closeButton.Content(winrt::box_value(winrt::hstring(L"Close")));
  // closeButton.Content(closeIcon);
  closeButton.Background(Brush(ButtonBg()));
  closeButton.Foreground(Brush(ChromeFg()));
  closeButton.BorderThickness(ThicknessHelper::FromUniformLength(0));
  closeButton.Padding(ThicknessHelper::FromLengths(18, 8, 18, 8));
  closeButton.Click([this](IInspectable const&, RoutedEventArgs const&) {
    HideExtensions();
  });
  Grid::SetColumn(closeButton, 1);
  header.Children().Append(closeButton);

  Grid::SetRow(header, 0);
  layout.Children().Append(header);

  extensionsList_ = StackPanel();
  extensionsList_.Margin(ThicknessHelper::FromLengths(0, 12, 0, 12));

  auto scroll = ScrollViewer();
  scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
  scroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
  scroll.Content(extensionsList_);
  Grid::SetRow(scroll, 1);
  layout.Children().Append(scroll);

  auto actions = StackPanel();
  actions.Orientation(Orientation::Horizontal);
  actions.HorizontalAlignment(HorizontalAlignment::Right);

  // auto addIcon = FontIcon();
  // addIcon.FontFamily(FontFamily(L"Segoe MDL2 Assets"));
  // addIcon.Glyph(L"\uE710");
  auto addButton = Button();
  addButton.Content(winrt::box_value(winrt::hstring(L"Add extension")));
  // addButton.Content(addIcon);
  addButton.Background(Brush(Accent()));
  addButton.Foreground(Brush(ChromeFg()));
  addButton.BorderThickness(ThicknessHelper::FromUniformLength(0));
  addButton.Padding(ThicknessHelper::FromLengths(18, 8, 18, 8));
  addButton.Click([this](IInspectable const&, RoutedEventArgs const&) {
    AddExtension();
  });
  actions.Children().Append(addButton);

  Grid::SetRow(actions, 2);
  layout.Children().Append(actions);

  extensionsPanel_.Child(layout);
  contentHost_.Children().Append(extensionsPanel_);
}

void MainPage::BuildLogsPanel() {
  logsPanel_ = Border();
  logsPanel_.Background(Brush(ErrorBg()));
  logsPanel_.Visibility(Visibility::Collapsed);

  auto layout = Grid();
  layout.Margin(ThicknessHelper::FromLengths(16, 12, 16, 12));

  auto headerRow = RowDefinition();
  headerRow.Height(GridLengthHelper::Auto());
  auto bodyRow = RowDefinition();
  bodyRow.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  layout.RowDefinitions().Append(headerRow);
  layout.RowDefinitions().Append(bodyRow);

  auto header = Grid();
  auto titleColumn = ColumnDefinition();
  titleColumn.Width(
      GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  auto closeColumn = ColumnDefinition();
  closeColumn.Width(GridLengthHelper::Auto());
  header.ColumnDefinitions().Append(titleColumn);
  header.ColumnDefinitions().Append(closeColumn);

  auto title = TextBlock();
  title.Text(L"Application logs");
  title.FontSize(20);
  title.FontWeight(FontWeights::Bold());
  title.Foreground(Brush(ChromeFg()));
  title.VerticalAlignment(VerticalAlignment::Center);
  Grid::SetColumn(title, 0);
  header.Children().Append(title);

  auto closeButton = Button();
  closeButton.Content(winrt::box_value(winrt::hstring(L"Close")));
  closeButton.Background(Brush(ButtonBg()));
  closeButton.Foreground(Brush(ChromeFg()));
  closeButton.BorderThickness(ThicknessHelper::FromUniformLength(0));
  closeButton.Padding(ThicknessHelper::FromLengths(18, 8, 18, 8));
  closeButton.Click([this](IInspectable const&, RoutedEventArgs const&) {
    HideLogs();
  });
  Grid::SetColumn(closeButton, 1);
  header.Children().Append(closeButton);

  Grid::SetRow(header, 0);
  layout.Children().Append(header);

  logsText_ = TextBlock();
  logsText_.FontFamily(FontFamily(L"Consolas"));
  logsText_.FontSize(11);
  logsText_.Foreground(Brush(MutedFg()));
  logsText_.TextWrapping(TextWrapping::Wrap);
  logsText_.IsTextSelectionEnabled(true);

  logsScroll_ = ScrollViewer();
  logsScroll_.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
  logsScroll_.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
  logsScroll_.Margin(ThicknessHelper::FromLengths(0, 12, 0, 0));
  logsScroll_.Content(logsText_);
  Grid::SetRow(logsScroll_, 1);
  layout.Children().Append(logsScroll_);

  logsPanel_.Child(layout);
  contentHost_.Children().Append(logsPanel_);
}

void MainPage::WireEvents() {
  goButton_.Click([this](IInspectable const&, RoutedEventArgs const&) {
    LoadFromAddressBar();
  });

  auto menu = MenuFlyout();

  auto backIcon = MakeGlyphIcon(L"\uE72B");
  backItem_ = MenuFlyoutItem();
  backItem_.Text(L"Back");
  backItem_.Icon(backIcon);
  backItem_.Click([this](IInspectable const&, RoutedEventArgs const&) {
    if (web_.CanGoBack()) {
      web_.GoBack();
    }
  });
  menu.Items().Append(backItem_);

  auto forwardIcon = MakeGlyphIcon(L"\uE72A");
  forwardItem_ = MenuFlyoutItem();
  forwardItem_.Text(L"Forward");
  forwardItem_.Icon(forwardIcon);
  forwardItem_.Click([this](IInspectable const&, RoutedEventArgs const&) {
    if (web_.CanGoForward()) {
      web_.GoForward();
    }
  });
  menu.Items().Append(forwardItem_);

  menu.Items().Append(MenuFlyoutSeparator());

  auto adaptIcon = MakeGlyphIcon(L"\uE774");
  auto adaptItem = MenuFlyoutItem();
  adaptItem.Text(L"Adapt page");
  adaptItem.Icon(adaptIcon);
  adaptItem.Click([this](IInspectable const&, RoutedEventArgs const&) {
    NavigateToAdapted();
  });
  menu.Items().Append(adaptItem);

  menu.Items().Append(MenuFlyoutSeparator());

  auto extensionsIcon = MakeGlyphIcon(L"\uE71D");
  auto extensionsItem = MenuFlyoutItem();
  extensionsItem.Text(L"Extensions");
  extensionsItem.Icon(extensionsIcon);
  extensionsItem.Click([this](IInspectable const&, RoutedEventArgs const&) {
    ShowExtensions();
  });
  menu.Items().Append(extensionsItem);

  auto settingsIcon = MakeGlyphIcon(L"\uE713");
  auto settingsItem = MenuFlyoutItem();
  settingsItem.Text(L"Settings");
  settingsItem.Icon(settingsIcon);
  settingsItem.Click([this](IInspectable const&, RoutedEventArgs const&) {
    ShowSettings();
  });
  menu.Items().Append(settingsItem);

  auto pinIcon = MakeGlyphIcon(L"\uE718");
  auto pinItem = MenuFlyoutItem();
  pinItem.Text(L"Pin to start screen");
  pinItem.Icon(pinIcon);
  pinItem.Click([this](IInspectable const&, RoutedEventArgs const&) {
    PinCurrentPage();
  });
  menu.Items().Append(pinItem);
  menuButton_.Flyout(menu);

  reloadButton_.Click([this](IInspectable const&, RoutedEventArgs const&) {
    if (errorPanel_.Visibility() == Visibility::Visible && !currentUrl_.empty()) {
      NavigateTo(currentUrl_);
      return;
    }
    try {
      web_.Refresh();
    } catch (...) {
      NavigateTo(HomeUrl());
    }
  });

  retryButton_.Click([this](IInspectable const&, RoutedEventArgs const&) {
    NavigateTo(currentUrl_.empty() ? HomeUrl() : currentUrl_);
  });

  addressBar_.KeyDown([this](IInspectable const&, KeyRoutedEventArgs const& args) {
    if (args.Key() == VirtualKey::Enter) {
      args.Handled(true);
      LoadFromAddressBar();
    }
  });

  web_.NavigationStarting([this](WebView const&, WebViewNavigationStartingEventArgs const& args) {
        HideError();
        progress_.Visibility(Visibility::Visible);
        longScriptStopRequested_ = false;
        UpdateChrome();

        auto uri = args.Uri();
        if (!uri) {
          log::Write(L"main: navigation starting without uri");
          return;
        }
        log::Write(L"main: navigation starting " +
                   std::wstring(uri.AbsoluteUri().c_str()));

        const winrt::hstring scheme = uri.SchemeName();
        if (!IsInternalScheme(scheme)) {
          args.Cancel(true);
          progress_.Visibility(Visibility::Collapsed);
          log::Write(L"main: handing off " + std::wstring(uri.AbsoluteUri()));
          LaunchExternal(uri);
          return;
        }

        currentUrl_ = uri.AbsoluteUri();
        addressBar_.Text(currentUrl_);
        statusText_.Text(L"Loading\u2026");
        statusText_.Visibility(Visibility::Visible);
      });

  web_.NavigationCompleted([this](WebView const&, WebViewNavigationCompletedEventArgs const& args) {
        progress_.Visibility(Visibility::Collapsed);
        log::Write(L"main: navigation completed success=" +
                   std::wstring(args.IsSuccess() ? L"true" : L"false"));

        if (auto uri = args.Uri()) {
          currentUrl_ = uri.AbsoluteUri();
          addressBar_.Text(currentUrl_);
        }

        if (args.IsSuccess()) {
          HideError();
          ApplyExtensionScripts();
          const winrt::hstring title = web_.DocumentTitle();
          statusText_.Text(title.empty() ? winrt::hstring(L"Ready") : title);
          statusText_.Visibility(Visibility::Visible);
        } else {
          const winrt::hstring detail = DescribeError(args.WebErrorStatus());
          ShowError(L"Can't reach this page", detail);
          statusText_.Text(L"Load failed");
          statusText_.Visibility(Visibility::Visible);
        }

        if (!extensionsLoadStarted_) {
          extensionsLoadStarted_ = true;
          LoadExtensionsAsync();
        }
        UpdateChrome();
      });

  web_.ContentLoading([this](WebView const&, WebViewContentLoadingEventArgs const&) {
        log::Write(L"main: content loading");
        ApplyExtensionScripts();
      });

  web_.DOMContentLoaded([this](WebView const&, WebViewDOMContentLoadedEventArgs const&) {
        log::Write(L"main: dom content loaded");
        ApplyExtensionScripts();
      });

  web_.ContainsFullScreenElementChanged([this](WebView const& sender, IInspectable const&) {
        try {
          auto view = ApplicationView::GetForCurrentView();
          if (sender.ContainsFullScreenElement()) {
            if (!view.IsFullScreenMode()) {
              view.TryEnterFullScreenMode();
            }
          } else if (view.IsFullScreenMode()) {
            view.ExitFullScreenMode();
          }
        } catch (winrt::hresult_error const& error) {
          log::Write(L"main: full screen failed " + std::wstring(error.message()));
        } catch (...) {
          log::Write(L"main: full screen failed");
        }
      });

  web_.PermissionRequested([this](WebView const&, WebViewPermissionRequestedEventArgs const& args) {
        auto request = args.PermissionRequest();
        if (!request) {
          return;
        }
        try {
          request.Defer();
        } catch (winrt::hresult_error const& error) {
          log::Write(L"main: permission defer failed " + std::wstring(error.message()));
          request.Deny();
          return;
        } catch (...) {
          request.Deny();
          return;
        }
        const std::wstring host = request.Uri()
                                      ? std::wstring(request.Uri().Host())
                                      : std::wstring(L"This site");
        const std::wstring message = host + L" wants to use " + PermissionName(request.PermissionType()) + L".";
        log::Write(L"main: permission requested " + message);
        try {
          request.Deny();
        } catch (...) {
        }
      });

  web_.LongRunningScriptDetected([this](WebView const&, WebViewLongRunningScriptDetectedEventArgs const& args) {
        try {
          if (longScriptStopRequested_) {
            return;
          }
          const double elapsedMs = std::chrono::duration<double, std::milli>(args.ExecutionTime()).count();
          if (elapsedMs < kLongScriptThresholdMs) {
            return;
          }
          longScriptStopRequested_ = true;
          log::Write(L"main: long running script " +
                     std::to_wstring(static_cast<long long>(elapsedMs)) +
                     L"ms, stopping");
          args.StopPageScriptExecution(true);
          log::Write(L"main: stopped long running script");
        } catch (winrt::hresult_error const& error) {
          log::Write(L"main: long running script failed " + std::wstring(error.message()));
        } catch (...) {
          log::Write(L"main: long running script failed");
        }
      });

  web_.UnsafeContentWarningDisplaying([this](WebView const&, IInspectable const&) {
        log::Write(L"main: unsafe content warning");
        /*
        statusText_.Text(L"This page was reported as unsafe.");
        statusText_.Visibility(Visibility::Visible);
        */
      });

  web_.UnviewableContentIdentified([this](WebView const&, WebViewUnviewableContentIdentifiedEventArgs const& args) {
        std::wstring mediaType;
        try {
          mediaType = std::wstring(args.MediaType());
        } catch (...) {
        }
        auto uri = args.Uri();
        std::wstring target;
        if (uri) {
          target = uri.AbsoluteUri();
        }
        log::Write(L"main: unviewable content " + mediaType + L" " + target);
        progress_.Visibility(Visibility::Collapsed);
        if (uri) {
          DownloadContentAsync(target, mediaType);
        }
      });

  if (ApiInformation::IsEventPresent(L"Windows.UI.Xaml.Controls.WebView", L"NewWindowRequested")) {
    web_.NewWindowRequested(
        [this](WebView const&, WebViewNewWindowRequestedEventArgs const& args) {
          args.Handled(true);
          if (auto uri = args.Uri()) {
            log::Write(L"main: new window navigate to " + std::wstring(uri.AbsoluteUri()));
            NavigateTo(uri.AbsoluteUri());
          }
        });
  }

  if (ApiInformation::IsEventPresent(L"Windows.UI.Xaml.Controls.WebView", L"UnsupportedUriSchemeIdentified")) {
    web_.UnsupportedUriSchemeIdentified(
        [this](WebView const&,
               WebViewUnsupportedUriSchemeIdentifiedEventArgs const& args) {
          args.Handled(true);
          if (auto uri = args.Uri()) {
            const winrt::hstring scheme = uri.SchemeName();
            const std::wstring target = std::wstring(uri.AbsoluteUri());
            if (scheme == L"intent") {
              log::Write(L"main: ignoring intent uri scheme " + target);
              return;
            }
            log::Write(L"main: unsupported uri scheme " + target);
            LaunchExternal(uri);
          }
        });
  }

}

void MainPage::WireSystemBack() {
  try {
    SystemNavigationManager::GetForCurrentView().BackRequested(
        [this](IInspectable const&, BackRequestedEventArgs const& args) {
          args.Handled(HandleBack());
        });
  } catch (...) {
    log::Write(L"main: system back is unavailable");
  }
}

void MainPage::ApplySystemChrome() {
  try {
    auto view = ApplicationView::GetForCurrentView();
    view.SetDesiredBoundsMode(ApplicationViewBoundsMode::UseVisible);
    view.VisibleBoundsChanged([this](IInspectable const&, IInspectable const&) {
      ApplyVisibleBounds();
    });
    ApplyVisibleBounds();
  } catch (...) {
  }

  try {
    if (ApiInformation::IsTypePresent(L"Windows.UI.ViewManagement.StatusBar")) {
      auto bar = StatusBar::GetForCurrentView();
      bar.BackgroundColor(ChromeBg());
      bar.BackgroundOpacity(1.0);
      bar.ForegroundColor(ChromeFg());
    }
  } catch (...) {
  }
}

void MainPage::ApplyVisibleBounds() {
  try {
    auto view = ApplicationView::GetForCurrentView();
    const Rect visible = view.VisibleBounds();
    const Rect window = Window::Current().Bounds();

    auto sane = [](double value) {
      return (value > 0 && value < 400) ? value : 0.0;
    };
    const double left = visible.X - window.X;
    const double top = visible.Y - window.Y;
    const double right = (window.X + window.Width) - (visible.X + visible.Width);
    const double bottom =
        (window.Y + window.Height) - (visible.Y + visible.Height);
    root_.Padding(ThicknessHelper::FromLengths(sane(left), sane(top),
                                               sane(right), sane(bottom)));
  } catch (...) {
  }
}

void MainPage::LoadFromAddressBar() {
  NavigateTo(addressBar_.Text());
  try {
    web_.Focus(FocusState::Programmatic);
  } catch (...) {
  }
}

void MainPage::NavigateToAdapted() {
  if (currentUrl_.empty()) {
    return;
  }

  try {
    const Uri current{currentUrl_};
    const std::wstring scheme = std::wstring(current.SchemeName());
    if (_wcsicmp(scheme.c_str(), L"http") != 0 &&
        _wcsicmp(scheme.c_str(), L"https") != 0) {
      statusText_.Text(L"Only http and https pages can be adapted.");
      statusText_.Visibility(Visibility::Visible);
      return;
    }

    const std::wstring localPath = content::LocalPathFromUri(current);
    if (localPath.empty()) {
      statusText_.Text(L"This address can't be adapted.");
      statusText_.Visibility(Visibility::Visible);
      return;
    }

    if (!resolver_) {
      resolver_ = winrt::make<content::StreamResolver>()
                      .as<winrt::Windows::Web::IUriToStreamResolver>();
    }

    const size_t suffixStart = localPath.find_first_of(L"?#");
    const std::wstring basePath = localPath.substr(0, suffixStart);
    const std::wstring suffix =
        suffixStart == std::wstring::npos ? std::wstring()
                                          : localPath.substr(suffixStart);
    const Uri localBase = web_.BuildLocalStreamUri(
        winrt::hstring(content::kContentId),
        winrt::hstring(L"/" + basePath));
    std::wstring localText = std::wstring(localBase.AbsoluteUri());
    localText += suffix;
    const Uri local{winrt::hstring(localText)};
    currentUrl_ = local.AbsoluteUri();
    addressBar_.Text(currentUrl_);
    HideError();
    log::Write(L"main: navigate adapted " + std::wstring(currentUrl_));
    web_.NavigateToLocalStreamUri(local, resolver_);
  } catch (winrt::hresult_error const& error) {
    log::Write(L"main: adapt failed " + std::wstring(error.message()));
    statusText_.Text(L"This page can't be adapted.");
    statusText_.Visibility(Visibility::Visible);
  } catch (...) {
    log::Write(L"main: adapt failed");
    statusText_.Text(L"This page can't be adapted.");
    statusText_.Visibility(Visibility::Visible);
  }
}

void MainPage::NavigateTo(winrt::hstring const& input) {
  const winrt::hstring resolved = ResolveInput(input);
  if (resolved.empty()) {
    return;
  }

  try {
    const Uri uri{resolved};
    currentUrl_ = uri.AbsoluteUri();
    addressBar_.Text(currentUrl_);
    HideError();
    log::Write(L"main: navigate " + std::wstring(currentUrl_));

    web_.Navigate(uri);
  } catch (winrt::hresult_error const& error) {
    log::Write(L"main: bad address " + std::wstring(error.message()));
    ShowError(L"Invalid address",
              L"That does not look like an address this browser can open.");
  }
}

void MainPage::OpenExternalUrl(winrt::hstring const& url) {
  log::Write(L"main: external url " + std::wstring(url));
  NavigateTo(url);
}

void MainPage::UpdateChrome() {
  try {
    const bool canBack = web_.CanGoBack();
    const bool canForward = web_.CanGoForward();
    if (backItem_) {
      backItem_.IsEnabled(canBack);
    }
    if (forwardItem_) {
      forwardItem_.IsEnabled(canForward);
    }
    SystemNavigationManager::GetForCurrentView().AppViewBackButtonVisibility(
        canBack ? AppViewBackButtonVisibility::Visible
                : AppViewBackButtonVisibility::Collapsed);
  } catch (...) {
  }
}

void MainPage::ShowError(winrt::hstring const& title,
                         winrt::hstring const& detail, bool includeUrl) {
  errorTitle_.Text(title);
  if (includeUrl && !currentUrl_.empty()) {
    errorText_.Text(detail + winrt::hstring(L"\n") + currentUrl_);
  } else {
    errorText_.Text(detail);
  }
  // A XAML WebView draws in its own surface, so an overlay above it is not
  // reliable on Windows 10 Mobile. Hide the view instead of fighting airspace.
  web_.Visibility(Visibility::Collapsed);
  errorPanel_.Visibility(Visibility::Visible);
}

void MainPage::HideError() {
  errorPanel_.Visibility(Visibility::Collapsed);
  web_.Visibility(Visibility::Visible);
}

void MainPage::AddExtension() {
  AddExtensionAsync();
}

winrt::fire_and_forget MainPage::AddExtensionAsync() {
  using namespace winrt::Windows::Storage;
  using namespace winrt::Windows::Storage::Pickers;

  try {
    FileOpenPicker picker;
    picker.SuggestedStartLocation(PickerLocationId::Downloads);
    picker.FileTypeFilter().Append(L".zip");
    picker.FileTypeFilter().Append(L".js");
    picker.FileTypeFilter().Append(L".html");

    const StorageFile file = co_await picker.PickSingleFileAsync();
    if (!file) {
      co_return;
    }

    statusText_.Text(L"Adding extension\u2026");
    statusText_.Visibility(Visibility::Visible);
    log::Write(L"extension: installing " + std::wstring(file.Name()));

    const winrt::hstring id = co_await extensions::InstallAsync(file);
    log::Write(L"extension: installed " + std::wstring(id));
    co_await extensions::Manager::Get().LoadAsync(std::wstring(id.c_str()));
    RefreshExtensionsList();
    ApplyExtensionScripts();
    statusText_.Text(L"Extension added");
    statusText_.Visibility(Visibility::Visible);
  } catch (winrt::hresult_error const& error) {
    log::Write(L"extension: failed " + std::wstring(error.message()));
    statusText_.Text(L"Could not add extension");
    statusText_.Visibility(Visibility::Visible);
    HideExtensions();
    ShowError(L"Could not add extension", error.message(), false);
  } catch (...) {
    log::Write(L"extension: failed");
    statusText_.Text(L"Could not add extension");
    statusText_.Visibility(Visibility::Visible);
    HideExtensions();
    ShowError(L"Could not add extension",
              L"The extension could not be installed.", false);
  }
}

void MainPage::ShowSettings() {
  std::wstring search = LoadSetting(kSearchUrlKey);
  if (search.empty()) {
    search = kDefaultSearchUrl;
  }
  searchBox_.Text(winrt::hstring(search));
  std::wstring home = LoadSetting(kHomeUrlKey);
  if (home.empty()) {
    home = kDefaultHomeUrl;
  }
  homeBox_.Text(winrt::hstring(home));
  std::wstring userAgent = LoadSetting(kUserAgentKey);
  if (userAgent.empty()) {
    userAgent = kDefaulUserAgent;
  }
  userAgentBox_.Text(winrt::hstring(userAgent));
  std::wstring updateUrl = LoadSetting(kUpdateUrlKey);
  updateUrlBox_.Text(winrt::hstring(updateUrl));
  const bool logsEnabled = LoadSetting(log::kEnabledSettingKey) == L"1";
  SetCheckBoxChecked(logsCheckBox_, logsEnabled);
  logsButton_.IsEnabled(logsEnabled);
  RefreshExtensionsList();
  web_.Visibility(Visibility::Collapsed);
  settingsPanel_.Visibility(Visibility::Visible);
}

void MainPage::HideSettings() {
  settingsPanel_.Visibility(Visibility::Collapsed);
  if (errorPanel_.Visibility() != Visibility::Visible) {
    web_.Visibility(Visibility::Visible);
  }
}

void MainPage::ShowExtensions() {
  RefreshExtensionsList();
  web_.Visibility(Visibility::Collapsed);
  extensionsPanel_.Visibility(Visibility::Visible);
}

void MainPage::HideExtensions() {
  extensionsPanel_.Visibility(Visibility::Collapsed);
  if (errorPanel_.Visibility() != Visibility::Visible) {
    web_.Visibility(Visibility::Visible);
  }
}

void MainPage::ShowLogs() {
  if (!log::Enabled()) {
    return;
  }
  const std::wstring text = log::Read();
  logsText_.Text(text.empty() ? winrt::hstring(L"No log entries yet.")
                              : winrt::hstring(text));
  logsPanel_.Visibility(Visibility::Visible);
  try {
    logsScroll_.UpdateLayout();
    logsScroll_.ChangeView(nullptr, logsScroll_.ScrollableHeight(), nullptr, true);
  } catch (...) {
  }
}

void MainPage::HideLogs() {
  logsPanel_.Visibility(Visibility::Collapsed);
}

void MainPage::SaveSettings() {
  const std::wstring previousUserAgent = std::wstring(UserAgent());
  const std::wstring search = Trim(std::wstring(searchBox_.Text()));
  const std::wstring home = Trim(std::wstring(homeBox_.Text()));
  const std::wstring userAgent = Trim(std::wstring(userAgentBox_.Text()));
  const std::wstring updateUrl = Trim(std::wstring(updateUrlBox_.Text()));
  const bool logsEnabled = IsCheckBoxChecked(logsCheckBox_);
  const bool defaultSearch = search.empty() || search == kDefaultSearchUrl;
  const bool defaultHome = home.empty() || home == kDefaultHomeUrl;
  const bool defaultUserAgent = userAgent.empty() || userAgent == kDefaulUserAgent;
  StoreSetting(kSearchUrlKey, defaultSearch ? std::wstring() : search);
  StoreSetting(kHomeUrlKey, defaultHome ? std::wstring() : home);
  StoreSetting(kUserAgentKey, defaultUserAgent ? std::wstring() : userAgent);
  StoreSetting(kUpdateUrlKey, updateUrl);
  StoreSetting(log::kEnabledSettingKey,
               logsEnabled ? std::wstring(L"1") : std::wstring());
  SetDefaultUserAgent(userAgent);
  log::SetEnabled(logsEnabled);
  logsButton_.IsEnabled(logsEnabled);
  log::Write(L"main: settings saved");
  HideSettings();
  if (userAgent != previousUserAgent && !currentUrl_.empty()) {
    NavigateTo(currentUrl_);
  }
}

void MainPage::PinCurrentPage() {
  const std::wstring url =
      currentUrl_.empty() ? std::wstring(HomeUrl()) : std::wstring(currentUrl_);
  const std::wstring tileId =
      L"url-" + std::to_wstring(std::hash<std::wstring>{}(url));

  std::wstring display = web_.DocumentTitle().c_str();
  if (display.empty()) {
    display = L"EdgeMinus";
  }
  if (display.size() > 40) {
    display.resize(40);
  }

  PinCurrentPageAsync(tileId, display, L"url|" + url);
}

winrt::fire_and_forget MainPage::PinCurrentPageAsync(
    std::wstring tileId, std::wstring display,
    std::wstring argumentsText) {
  using namespace winrt::Windows::Data::Json;
  using namespace winrt::Windows::Graphics::Imaging;
  using namespace winrt::Windows::Storage;
  using namespace winrt::Windows::Storage::Streams;
  using namespace winrt::Windows::UI::StartScreen;
  using namespace winrt::Windows::Web::Http;

  std::wstring logo150 = L"ms-appx:///Assets/Square150x150Logo.png";
  std::wstring logo71 = L"ms-appx:///Assets/Square71x71Logo.png";
  std::wstring logo44 = L"ms-appx:///Assets/Square44x44Logo.png";

  try {
    const std::wstring script = LR"JS(
      (function () {
        var best = '';
        var score = -1;
        var links = document.querySelectorAll('link[rel]');
        for (var i = 0; i < links.length; i++) {
          var el = links[i];
          var rel = (el.getAttribute('rel') || '').toLowerCase();
          if (rel.indexOf('icon') < 0 &&
              rel.indexOf('apple-touch') < 0) {
            continue;
          }
          var href = el.getAttribute('href');
          if (!href) continue;
          var value = rel.indexOf('apple-touch') >= 0 ? 1000 : 0;
          var sizes = (el.getAttribute('sizes') || '').toLowerCase();
          var match = sizes.match(/(\d+)x(\d+)/);
          if (match) {
            value += parseInt(match[1], 10) * parseInt(match[2], 10);
          }
          var type = (el.getAttribute('type') || '').toLowerCase();
          if (type.indexOf('svg') >= 0) value -= 1000;
          if (value > score) {
            score = value;
            best = href;
          }
        }
        if (!best) best = '/favicon.ico';
        try {
          return new URL(best, document.baseURI || location.href).href;
        } catch (e) {
          return '';
        }
      })();
    )JS";

    std::wstring iconUrl;
    const winrt::hstring result = co_await web_.InvokeScriptAsync(
        L"eval", {winrt::hstring(script)});
    if (!result.empty()) {
      const std::wstring raw(result.c_str());
      try {
        iconUrl = std::wstring(JsonValue::Parse(result).GetString().c_str());
      } catch (...) {
        iconUrl = raw;
      }
    }

    if (!iconUrl.empty()) {
      log::Write(L"main: getting icon at " + iconUrl);
      const winrt::Windows::Foundation::Uri iconUri{
          winrt::hstring(iconUrl)};
      const std::wstring scheme(iconUri.SchemeName());
      if (scheme == L"http" || scheme == L"https") {
        HttpClient client;
        HttpRequestMessage request(HttpMethod::Get(), iconUri);
        request.Headers().TryAppendWithoutValidation(
            L"Accept", L"image/*,*/*;q=0.8");
        const auto response = co_await client.SendRequestAsync(request);
        if (response.IsSuccessStatusCode()) {
          const auto buffer = co_await response.Content().ReadAsBufferAsync();
          if (buffer.Length() <= kMaxFaviconBytes) {
            InMemoryRandomAccessStream memory;
            co_await memory.WriteAsync(buffer);
            memory.Seek(0);

            const auto folder = co_await ApplicationData::Current()
                                    .LocalFolder()
                                    .CreateFolderAsync(
                                        L"tiles",
                                        CreationCollisionOption::OpenIfExists);

            const auto file150 = co_await folder.CreateFileAsync(
                winrt::hstring(tileId + L"-150.png"),
                CreationCollisionOption::ReplaceExisting);
            const auto file71 = co_await folder.CreateFileAsync(
                winrt::hstring(tileId + L"-71.png"),
                CreationCollisionOption::ReplaceExisting);
            const auto file44 = co_await folder.CreateFileAsync(
                winrt::hstring(tileId + L"-44.png"),
                CreationCollisionOption::ReplaceExisting);

            co_await SaveScaledPngAsync(memory, 150, file150);
            co_await SaveScaledPngAsync(memory, 71, file71);
            co_await SaveScaledPngAsync(memory, 44, file44);

            logo150 = L"ms-appdata:///local/tiles/" + tileId + L"-150.png";
            logo71 = L"ms-appdata:///local/tiles/" + tileId + L"-71.png";
            logo44 = L"ms-appdata:///local/tiles/" + tileId + L"-44.png";
          }
        }
      }
    }
  } catch (winrt::hresult_error const& error) {
    log::Write(L"main: favicon failed " + std::wstring(error.message()));
  } catch (...) {
    log::Write(L"main: favicon failed");
  }

  try {
    SecondaryTile tile(
        winrt::hstring(tileId), winrt::hstring(display),
        winrt::hstring(display), winrt::hstring(argumentsText),
        TileOptions::None, winrt::Windows::Foundation::Uri(winrt::hstring(logo150)));
    tile.SmallLogo(winrt::Windows::Foundation::Uri(winrt::hstring(logo44)));
    tile.VisualElements().Square44x44Logo(winrt::Windows::Foundation::Uri(winrt::hstring(logo44)));
    tile.VisualElements().Square71x71Logo(winrt::Windows::Foundation::Uri(winrt::hstring(logo71)));
    tile.ForegroundText(ForegroundText::Light);
    const bool created = co_await tile.RequestCreateAsync();
    if (created) {
      log::Write(L"main: pinned current page");
    }
  } catch (winrt::hresult_error const& error) {
    log::Write(L"main: pin failed " + std::wstring(error.message()));
    ShowError(L"Could not pin", L"This page could not be pinned to Start.");
  }
}

winrt::fire_and_forget MainPage::CheckForUpdates(std::wstring url) {
  if (url.empty()) {
    log::Write(L"update: no update URL configured");
    statusText_.Text(L"Set the update URL in Settings.");
    statusText_.Visibility(Visibility::Visible);
    ShowSettings();
    co_return;
  }
  try {
    const Uri target{winrt::hstring(url)};
    const std::wstring scheme(target.SchemeName().c_str());
    if (_wcsicmp(scheme.c_str(), L"http") != 0 && _wcsicmp(scheme.c_str(), L"https") != 0) {
      log::Write(L"update: unsupported update URL " + url);
      statusText_.Text(L"Update URL must be an http or https address.");
      statusText_.Visibility(Visibility::Visible);
      ShowSettings();
      co_return;
    }
    PackageManager packageManager;
    log::Write(L"update: installing " + url);
    if (url.size() >= 5 && url.compare(url.size() - 5, 5, L".appx") == 0) {
      co_await packageManager.AddPackageAsync(
        target,
        nullptr,
        DeploymentOptions::ForceTargetApplicationShutdown
      );
    } else {
      co_await packageManager.AddPackageByAppInstallerFileAsync(
        target,
        AddPackageByAppInstallerOptions::ForceTargetAppShutdown,
        nullptr
      );
    }
  } catch (winrt::hresult_error const& error) {
    log::Write(L"update: launch failed " + std::wstring(error.message()));
    statusText_.Text(L"Could not open App Installer.");
    statusText_.Visibility(Visibility::Visible);
  }
}

winrt::fire_and_forget MainPage::DownloadContentAsync(std::wstring url,
                                                      std::wstring mediaType) {
  using namespace winrt::Windows::Storage;
  using namespace winrt::Windows::Web::Http;

  try {
    const Uri uri{winrt::hstring(url)};
    const std::wstring scheme(uri.SchemeName());
    if (_wcsicmp(scheme.c_str(), L"http") != 0 &&
        _wcsicmp(scheme.c_str(), L"https") != 0) {
      co_return;
    }

    const std::wstring name = FileNameFromUri(uri, mediaType);

    ContentDialog dialog;
    dialog.Title(winrt::box_value(winrt::hstring(L"Download content")));
    dialog.Content(winrt::box_value(winrt::hstring(
        L"This content can't be displayed in the browser.\n\nDownload \"" +
        name + L"\"?")));
    dialog.PrimaryButtonText(L"Download");
    dialog.CloseButtonText(L"Cancel");
    dialog.DefaultButton(ContentDialogButton::Primary);
    const ContentDialogResult result = co_await dialog.ShowAsync();
    if (result != ContentDialogResult::Primary) {
      co_return;
    }

    statusText_.Text(L"Downloading\u2026");
    statusText_.Visibility(Visibility::Visible);
    log::Write(L"main: downloading " + url);

    HttpClient client;
    HttpRequestMessage request(HttpMethod::Get(), uri);
    const auto response = co_await client.SendRequestAsync(request);
    if (!response.IsSuccessStatusCode()) {
      log::Write(L"main: download failed status " +
                 std::to_wstring(static_cast<int>(response.StatusCode())));
      statusText_.Text(L"Download failed");
      co_return;
    }

    const auto buffer = co_await response.Content().ReadAsBufferAsync();
    const StorageFile file = co_await DownloadsFolder::CreateFileAsync(
        winrt::hstring(name), CreationCollisionOption::GenerateUniqueName);
    co_await FileIO::WriteBufferAsync(file, buffer);

    const std::wstring saved(file.Name().c_str());
    log::Write(L"main: downloaded to " + saved);
    statusText_.Text(L"Saved " + saved + L" to Downloads");
    statusText_.Visibility(Visibility::Visible);
  } catch (winrt::hresult_error const& error) {
    log::Write(L"main: download failed " + std::wstring(error.message()));
    statusText_.Text(L"Download failed");
    statusText_.Visibility(Visibility::Visible);
  } catch (...) {
    log::Write(L"main: download failed");
    statusText_.Text(L"Download failed");
    statusText_.Visibility(Visibility::Visible);
  }
}

winrt::hstring MainPage::UserAgent() const {
  const std::wstring userAgent = LoadSetting(kUserAgentKey);
  return winrt::hstring(userAgent.empty() ? kDefaulUserAgent : userAgent);
}

void MainPage::RefreshExtensionsList() {
  if (!extensionsList_) {
    return;
  }
  extensionsList_.Children().Clear();

  const auto& installed = extensions::Manager::Get().All();
  if (installed.empty()) {
    auto empty = TextBlock();
    empty.Text(L"No extensions installed.");
    empty.FontSize(12);
    empty.Foreground(Brush(MutedFg()));
    extensionsList_.Children().Append(empty);
    return;
  }

  for (auto const& extension : installed) {
    auto row = Grid();
    row.Margin(ThicknessHelper::FromLengths(0, 0, 0, 6));

    auto infoColumn = ColumnDefinition();
    infoColumn.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    auto actionColumn = ColumnDefinition();
    actionColumn.Width(GridLengthHelper::Auto());
    row.ColumnDefinitions().Append(infoColumn);
    row.ColumnDefinitions().Append(actionColumn);

    std::wstring label =
        extension.name.empty() ? extension.id : extension.name;
    if (!extension.patterns.empty()) {
      label += L"  " + extension.patterns.front();
    }

    auto text = TextBlock();
    text.Text(winrt::hstring(label));
    text.FontSize(12);
    text.Foreground(Brush(ChromeFg()));
    text.TextTrimming(TextTrimming::CharacterEllipsis);
    text.VerticalAlignment(VerticalAlignment::Center);
    text.Margin(ThicknessHelper::FromLengths(0, 0, 8, 0));
    Grid::SetColumn(text, 0);
    row.Children().Append(text);

    auto actions = StackPanel();
    actions.Orientation(Orientation::Horizontal);
    actions.VerticalAlignment(VerticalAlignment::Center);

    const std::wstring id = extension.id;
    if (extension.hasIndexHtml) {
      auto openIcon = MakeGlyphIcon(L"\uE8A7");
      auto openButton = Button();
      //openButton.Content(winrt::box_value(winrt::hstring(L"Open")));
      openButton.Content(openIcon);
      openButton.Background(Brush(ButtonBg()));
      openButton.Foreground(Brush(ChromeFg()));
      openButton.BorderThickness(ThicknessHelper::FromUniformLength(0));
      openButton.Padding(ThicknessHelper::FromLengths(12, 6, 12, 6));
      openButton.Margin(ThicknessHelper::FromLengths(0, 0, 6, 0));
      openButton.Click([this, id](IInspectable const&, RoutedEventArgs const&) {
        NavigateTo(winrt::hstring(L"ms-appdata:///local/extensions/" + id + L"/index.html"));
        HideExtensions();
      });
      actions.Children().Append(openButton);
    }

    auto removeIcon = MakeGlyphIcon(L"\uE74D");
    auto removeButton = Button();
    //removeButton.Content(winrt::box_value(winrt::hstring(L"Remove")));
    removeButton.Content(removeIcon);
    removeButton.Background(Brush(ButtonBg()));
    removeButton.Foreground(Brush(ChromeFg()));
    removeButton.BorderThickness(ThicknessHelper::FromUniformLength(0));
    removeButton.Padding(ThicknessHelper::FromLengths(12, 6, 12, 6));
    removeButton.Click([this, id](IInspectable const&, RoutedEventArgs const&) {
      RemoveExtensionAsync(id);
    });
    actions.Children().Append(removeButton);

    Grid::SetColumn(actions, 1);
    row.Children().Append(actions);

    extensionsList_.Children().Append(row);
  }
}

winrt::fire_and_forget MainPage::RemoveExtensionAsync(std::wstring id) {
  try {
    co_await extensions::Manager::Get().RemoveAsync(id);
    log::Write(L"extension: removed " + id);
    RefreshExtensionsList();
    statusText_.Text(L"Extension removed");
    statusText_.Visibility(Visibility::Visible);
  } catch (winrt::hresult_error const& error) {
    log::Write(L"extension: remove failed " + std::wstring(error.message()));
    statusText_.Text(L"Could not remove extension");
    statusText_.Visibility(Visibility::Visible);
  } catch (...) {
    log::Write(L"extension: remove failed");
    statusText_.Text(L"Could not remove extension");
    statusText_.Visibility(Visibility::Visible);
  }
}

winrt::fire_and_forget MainPage::LoadExtensionsAsync() {
  log::Write(L"extensions: request load");
  try {
    co_await extensions::Manager::Get().LoadAllAsync();
    log::Write(L"extensions: request load done " +
               std::to_wstring(extensions::Manager::Get().All().size()));
    RefreshExtensionsList();
    ApplyExtensionScripts();
  } catch (winrt::hresult_error const& error) {
    log::Write(L"extensions: load failed " +
               std::wstring(error.message()));
  } catch (...) {
    log::Write(L"extensions: load failed");
  }
}

void MainPage::ApplyExtensionScripts() {
  if (currentUrl_.empty()) {
    return;
  }
  const std::wstring script = extensions::Manager::Get().BuildInjectionScript(
      std::wstring(currentUrl_.c_str()));
  if (script.empty()) {
    return;
  }
  log::Write(L"extensions: injecting into " +
             std::wstring(currentUrl_.c_str()));
  try {
    web_.InvokeScriptAsync(L"eval", {winrt::hstring(script)});
  } catch (...) {
    log::Write(L"extensions: injection failed");
  }
}

bool MainPage::HandleBack() {
  if (logsPanel_.Visibility() == Visibility::Visible) {
    HideLogs();
    return true;
  }
  if (settingsPanel_.Visibility() == Visibility::Visible) {
    HideSettings();
    return true;
  }
  if (extensionsPanel_.Visibility() == Visibility::Visible) {
    HideExtensions();
    return true;
  }
  if (errorPanel_.Visibility() == Visibility::Visible) {
    HideError();
    if (web_.CanGoBack()) {
      web_.GoBack();
    }
    return true;
  }
  if (web_.CanGoBack()) {
    web_.GoBack();
    return true;
  }
  return false;
}

void MainPage::LaunchExternal(Uri const& uri) {
  try {
    (void)Launcher::LaunchUriAsync(uri);
  } catch (winrt::hresult_error const& error) {
    log::Write(L"main: launch failed " + std::wstring(error.message()));
    ShowError(L"Can't open this link",
              L"No application on this phone accepted it.");
  }
}

winrt::hstring MainPage::ResolveInput(winrt::hstring const& input) const {
  std::wstring text = Trim(std::wstring(input));

  if (text.empty()) {
    return HomeUrl();
  }
  std::wstring lower = text;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });

  const bool hasScheme = text.find(L"://") != std::wstring::npos;
  if (hasScheme || StartsWith(lower, L"about:") ||
      StartsWith(lower, L"file:") || StartsWith(lower, L"data:") ||
      StartsWith(lower, L"blob:") || StartsWith(lower, L"ms-appx:") ||
      StartsWith(lower, L"ms-appdata:") || StartsWith(lower, L"ms-local-stream:")) {
    return winrt::hstring(text);
  }
  if (StartsWith(lower, L"mailto:") || StartsWith(lower, L"tel:") ||
      StartsWith(lower, L"sms:") || StartsWith(lower, L"maps:") ||
      StartsWith(lower, L"ms-")) {
    return winrt::hstring(text);
  }
  const bool hasSpace = text.find(L' ') != std::wstring::npos;
  const bool looksLikeHost =
      !hasSpace &&
      (text.find(L'.') != std::wstring::npos ||
       StartsWith(lower, L"localhost") || StartsWith(lower, L"127.") ||
       StartsWith(lower, L"192.168.") || StartsWith(lower, L"10.") ||
       StartsWith(lower, L"[::1]"));

  if (looksLikeHost) {
    std::wstring result =
        (StartsWith(lower, L"localhost") || StartsWith(lower, L"127.") ||
         StartsWith(lower, L"192.168.") || StartsWith(lower, L"10.") ||
         StartsWith(lower, L"[::1]"))
            ? L"http://"
            : L"https://";
    result += text;
    return winrt::hstring(result);
  }
  const winrt::hstring escaped = Uri::EscapeComponent(winrt::hstring(text));
  std::wstring search = LoadSetting(kSearchUrlKey);
  if (search.empty()) {
    search = kDefaultSearchUrl;
  }
  const std::wstring token = L"{query}";
  const size_t placeholder = search.find(token);
  if (placeholder != std::wstring::npos) {
    search.replace(placeholder, token.size(), escaped.c_str());
  } else {
    search += escaped.c_str();
  }
  return winrt::hstring(search);
}

winrt::hstring MainPage::HomeUrl() {
  const std::wstring home = LoadSetting(kHomeUrlKey);
  return winrt::hstring(home.empty() ? kDefaultHomeUrl : home);
}

bool MainPage::IsInternalScheme(winrt::hstring const& scheme) {
  std::wstring value = scheme.c_str();
  std::transform(value.begin(), value.end(), value.begin(),
                 [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
  return value == L"http" || value == L"https" || value == L"file" ||
         value == L"about" || value == L"data" || value == L"blob" ||
         value == L"ms-appx" || value == L"ms-appx-web" ||
         value == L"ms-appdata" || value == L"ms-local-stream";
}

winrt::hstring MainPage::DescribeError(WebErrorStatus status) {
  switch (status) {
    case WebErrorStatus::HostNameNotResolved:
      return L"The server name could not be found. Check the address and the "
             L"connection.";
    case WebErrorStatus::CannotConnect:
    case WebErrorStatus::ServerUnreachable:
      return L"The server could not be reached.";
    case WebErrorStatus::Timeout:
    case WebErrorStatus::RequestTimeout:
      return L"The connection timed out.";
    case WebErrorStatus::ConnectionAborted:
    case WebErrorStatus::ConnectionReset:
    case WebErrorStatus::Disconnected:
      return L"The connection was interrupted.";
    case WebErrorStatus::CertificateCommonNameIsIncorrect:
    case WebErrorStatus::CertificateExpired:
    case WebErrorStatus::CertificateContainsErrors:
    case WebErrorStatus::CertificateRevoked:
    case WebErrorStatus::CertificateIsInvalid:
      return L"There is a problem with this site's security certificate.";
    case WebErrorStatus::NotFound:
      return L"The page was not found.";
    case WebErrorStatus::Forbidden:
      return L"The server refused to show this page.";
    case WebErrorStatus::Unauthorized:
      return L"This page needs a sign-in that was not provided.";
    case WebErrorStatus::ServiceUnavailable:
      return L"The server is temporarily unavailable.";
    case WebErrorStatus::BadGateway:
    case WebErrorStatus::GatewayTimeout:
      return L"The server did not answer correctly.";
    case WebErrorStatus::InternalServerError:
      return L"The site reported an internal error.";
    case WebErrorStatus::OperationCanceled:
      return L"The page load was cancelled.";
    default:
      return L"The page could not be loaded.";
  }
}

}  // namespace edgeminus
