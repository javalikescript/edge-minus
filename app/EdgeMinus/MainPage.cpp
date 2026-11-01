#include "pch.h"

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

Button MakeGlyphButton(std::wstring_view glyph) {
  FontIcon icon;
  icon.FontFamily(FontFamily(L"Segoe MDL2 Assets"));
  icon.FontSize(17);
  icon.Glyph(winrt::hstring(glyph));
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

constexpr wchar_t kSearchUrlKey[] = L"searchUrl";
constexpr wchar_t kUserAgentKey[] = L"userAgent";
constexpr wchar_t kHomeUrlKey[] = L"homeUrl";
constexpr wchar_t kUpdateUrlKey[] = L"updateUrl";
constexpr wchar_t kDefaultSearchUrl[] =
    L"https://www.bing.com/search?q={query}";
constexpr wchar_t kDefaultHomeUrl[] = L"about:blank";

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
  for (int i = 0; i < 6; ++i) {
    ColumnDefinition column;
    column.Width(i == 3 ? GridLengthHelper::FromValueAndType(1, GridUnitType::Star)
                        : GridLengthHelper::Auto());
    buttons.ColumnDefinitions().Append(column);
  }

  backButton_ = MakeGlyphButton(L"\uE72B");
  forwardButton_ = MakeGlyphButton(L"\uE72A");
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

  Grid::SetColumn(backButton_, 0);
  Grid::SetColumn(forwardButton_, 1);
  Grid::SetColumn(reloadButton_, 2);
  Grid::SetColumn(addressBar_, 3);
  Grid::SetColumn(goButton_, 4);
  Grid::SetColumn(menuButton_, 5);

  buttons.Children().Append(backButton_);
  buttons.Children().Append(forwardButton_);
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

  auto tools = StackPanel();
  tools.Orientation(Orientation::Horizontal);
  tools.Margin(ThicknessHelper::FromLengths(0, 0, 0, 20));

  auto logsButton = Button();
  logsButton.Content(winrt::box_value(winrt::hstring(L"View logs")));
  logsButton.Background(Brush(ButtonBg()));
  logsButton.Foreground(Brush(ChromeFg()));
  logsButton.BorderThickness(ThicknessHelper::FromUniformLength(0));
  logsButton.Padding(ThicknessHelper::FromLengths(18, 8, 18, 8));
  logsButton.Margin(ThicknessHelper::FromLengths(0, 0, 8, 0));
  logsButton.Click([this](IInspectable const&, RoutedEventArgs const&) {
    ShowLogs();
  });
  tools.Children().Append(logsButton);

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
  auto settingsItem = MenuFlyoutItem();
  settingsItem.Text(L"Settings");
  settingsItem.Click([this](IInspectable const&, RoutedEventArgs const&) {
    ShowSettings();
  });
  menu.Items().Append(settingsItem);

  auto pinItem = MenuFlyoutItem();
  pinItem.Text(L"Pin to start screen");
  pinItem.Click([this](IInspectable const&, RoutedEventArgs const&) {
    PinCurrentPage();
  });
  menu.Items().Append(pinItem);
  menuButton_.Flyout(menu);

  backButton_.Click([this](IInspectable const&, RoutedEventArgs const&) {
    if (web_.CanGoBack()) {
      web_.GoBack();
    }
  });

  forwardButton_.Click([this](IInspectable const&, RoutedEventArgs const&) {
    if (web_.CanGoForward()) {
      web_.GoForward();
    }
  });

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

  web_.NavigationStarting(
      [this](WebView const&, WebViewNavigationStartingEventArgs const& args) {
        HideError();
        progress_.Visibility(Visibility::Visible);
        UpdateChrome();

        auto uri = args.Uri();
        if (!uri) {
          return;
        }

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

  web_.NavigationCompleted(
      [this](WebView const&, WebViewNavigationCompletedEventArgs const& args) {
        progress_.Visibility(Visibility::Collapsed);

        if (auto uri = args.Uri()) {
          currentUrl_ = uri.AbsoluteUri();
          addressBar_.Text(currentUrl_);
        }

        if (args.IsSuccess()) {
          HideError();
          ApplyUserAgentScript();
          const winrt::hstring title = web_.DocumentTitle();
          statusText_.Text(title.empty() ? winrt::hstring(L"Ready") : title);
          statusText_.Visibility(Visibility::Visible);
        } else {
          const winrt::hstring detail = DescribeError(args.WebErrorStatus());
          ShowError(L"Can't reach this page", detail);
          statusText_.Text(L"Load failed");
          statusText_.Visibility(Visibility::Visible);
        }
        UpdateChrome();
      });

  web_.ContentLoading(
      [this](WebView const&, WebViewContentLoadingEventArgs const&) {
        ApplyUserAgentScript();
      });

  web_.DOMContentLoaded(
      [this](WebView const&, WebViewDOMContentLoadedEventArgs const&) {
        ApplyUserAgentScript();
      });

  if (ApiInformation::IsEventPresent(L"Windows.UI.Xaml.Controls.WebView",
                                     L"NewWindowRequested")) {
    web_.NewWindowRequested(
        [this](WebView const&, WebViewNewWindowRequestedEventArgs const& args) {
          args.Handled(true);
          if (auto uri = args.Uri()) {
            NavigateTo(uri.AbsoluteUri());
          }
        });
  }

  if (ApiInformation::IsEventPresent(L"Windows.UI.Xaml.Controls.WebView",
                                     L"UnsupportedUriSchemeIdentified")) {
    web_.UnsupportedUriSchemeIdentified(
        [this](WebView const&,
               WebViewUnsupportedUriSchemeIdentifiedEventArgs const& args) {
          args.Handled(true);
          if (auto uri = args.Uri()) {
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

    const winrt::hstring userAgent = UserAgent();
    const winrt::hstring scheme = uri.SchemeName();
    if (!userAgent.empty() && (scheme == L"http" || scheme == L"https")) {
      winrt::Windows::Web::Http::HttpRequestMessage request(
          winrt::Windows::Web::Http::HttpMethod::Get(), uri);
      request.Headers().TryAppendWithoutValidation(L"User-Agent", userAgent);
      web_.NavigateWithHttpRequestMessage(request);
    } else {
      web_.Navigate(uri);
    }
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
    backButton_.IsEnabled(canBack);
    forwardButton_.IsEnabled(canForward);
    SystemNavigationManager::GetForCurrentView().AppViewBackButtonVisibility(
        canBack ? AppViewBackButtonVisibility::Visible
                : AppViewBackButtonVisibility::Collapsed);
  } catch (...) {
  }
}

void MainPage::ShowError(winrt::hstring const& title,
                         winrt::hstring const& detail) {
  errorTitle_.Text(title);
  if (currentUrl_.empty()) {
    errorText_.Text(detail);
  } else {
    errorText_.Text(detail + winrt::hstring(L"\n") + currentUrl_);
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
  userAgentBox_.Text(winrt::hstring(LoadSetting(kUserAgentKey)));
  std::wstring updateUrl = LoadSetting(kUpdateUrlKey);
  if (updateUrl.empty()) {
    updateUrl = L"http://192.168.0.40/tmp/EdgeMinus_1.0.0.0_ARM.appx";
  }
  updateUrlBox_.Text(winrt::hstring(updateUrl));
  web_.Visibility(Visibility::Collapsed);
  settingsPanel_.Visibility(Visibility::Visible);
}

void MainPage::HideSettings() {
  settingsPanel_.Visibility(Visibility::Collapsed);
  if (errorPanel_.Visibility() != Visibility::Visible) {
    web_.Visibility(Visibility::Visible);
  }
}

void MainPage::ShowLogs() {
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
  const bool defaultSearch = search.empty() || search == kDefaultSearchUrl;
  const bool defaultHome = home.empty() || home == kDefaultHomeUrl;
  StoreSetting(kSearchUrlKey, defaultSearch ? std::wstring() : search);
  StoreSetting(kHomeUrlKey, defaultHome ? std::wstring() : home);
  StoreSetting(kUserAgentKey, userAgent);
  StoreSetting(kUpdateUrlKey, updateUrl);
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
    tile.BackgroundColor(ChromeBg());
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

winrt::hstring MainPage::UserAgent() const {
  return winrt::hstring(LoadSetting(kUserAgentKey));
}

void MainPage::ApplyUserAgentScript() {
  const std::wstring userAgent = std::wstring(UserAgent());
  if (userAgent.empty()) {
    return;
  }
  const std::wstring script =
      L"(function(){var u=" + JsString(userAgent) +
      L";var n=window.navigator;var p=Object.getPrototypeOf(n);"
      L"try{Object.defineProperty(n,'userAgent',{get:function(){return "
      L"u;},configurable:true});}catch(e){}"
      L"try{Object.defineProperty(p,'userAgent',{get:function(){return "
      L"u;},configurable:true});}catch(e){}"
      L"try{Object.defineProperty(n,'appVersion',{get:function(){return "
      L"u.replace(/^Mozilla\\//,'');},configurable:true});}catch(e){}"
      L"})();";
  try {
    web_.InvokeScriptAsync(L"eval", {winrt::hstring(script)});
  } catch (...) {
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
      StartsWith(lower, L"ms-appdata:")) {
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
         value == L"ms-appdata";
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
