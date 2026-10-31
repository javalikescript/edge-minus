#include "pch.h"

#include "MainPage.h"

#include "Log.h"

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Foundation::Metadata;
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
constexpr wchar_t kDefaultSearchUrl[] =
    L"https://www.bing.com/search?q={query}";

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
  searchHint.Text(
      L"Used for text that is not a URL. {query} is replaced with the terms.");
  searchHint.FontSize(11);
  searchHint.Foreground(Brush(MutedFg()));
  searchHint.TextWrapping(TextWrapping::Wrap);
  searchHint.Margin(ThicknessHelper::FromLengths(0, 4, 0, 16));
  stack.Children().Append(searchHint);

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
  settingsPanel_.Child(stack);
  contentHost_.Children().Append(settingsPanel_);
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
  userAgentBox_.Text(winrt::hstring(LoadSetting(kUserAgentKey)));
  web_.Visibility(Visibility::Collapsed);
  settingsPanel_.Visibility(Visibility::Visible);
}

void MainPage::HideSettings() {
  settingsPanel_.Visibility(Visibility::Collapsed);
  if (errorPanel_.Visibility() != Visibility::Visible) {
    web_.Visibility(Visibility::Visible);
  }
}

void MainPage::SaveSettings() {
  const std::wstring search = Trim(std::wstring(searchBox_.Text()));
  const std::wstring userAgent = Trim(std::wstring(userAgentBox_.Text()));
  const bool defaultSearch = search.empty() || search == kDefaultSearchUrl;
  StoreSetting(kSearchUrlKey, defaultSearch ? std::wstring() : search);
  StoreSetting(kUserAgentKey, userAgent);
  log::Write(L"main: settings saved");
  HideSettings();
}

void MainPage::PinCurrentPage() {
  const winrt::hstring url = currentUrl_.empty() ? HomeUrl() : currentUrl_;
  try {
    using winrt::Windows::UI::StartScreen::ForegroundText;
    using winrt::Windows::UI::StartScreen::SecondaryTile;
    using winrt::Windows::UI::StartScreen::TileOptions;

    const std::wstring tileId =
        L"url-" + std::to_wstring(std::hash<std::wstring>{}(std::wstring(url)));

    std::wstring display = web_.DocumentTitle().c_str();
    if (display.empty()) {
      display = L"EdgeMinus";
    }
    if (display.size() > 40) {
      display.resize(40);
    }

    std::wstring argumentsText = L"url|";
    argumentsText += std::wstring(url);

    SecondaryTile tile(
        winrt::hstring(tileId), winrt::hstring(display),
        winrt::hstring(display), winrt::hstring(argumentsText),
        TileOptions::None,
        winrt::Windows::Foundation::Uri(
            L"ms-appx:///Assets/Square150x150Logo.png"));
    tile.SmallLogo(winrt::Windows::Foundation::Uri(
        L"ms-appx:///Assets/Square44x44Logo.png"));
    tile.BackgroundColor(ChromeBg());
    tile.ForegroundText(ForegroundText::Light);
    tile.RequestCreateAsync().Completed(
        [](winrt::Windows::Foundation::IAsyncOperation<bool> const& operation,
           winrt::Windows::Foundation::AsyncStatus status) {
          try {
            if (status == winrt::Windows::Foundation::AsyncStatus::Completed &&
                operation.GetResults()) {
              log::Write(L"main: pinned current page");
            }
          } catch (...) {
          }
        });
  } catch (winrt::hresult_error const& error) {
    log::Write(L"main: pin failed " + std::wstring(error.message()));
    ShowError(L"Could not pin", L"This page could not be pinned to Start.");
  }
}

winrt::hstring MainPage::UserAgent() const {
  return winrt::hstring(LoadSetting(kUserAgentKey));
}

bool MainPage::HandleBack() {
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
  return L"https://www.bing.com/";
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
