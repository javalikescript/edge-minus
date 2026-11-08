#pragma once

#include "pch.h"

namespace edgeminus {

class MainPage {
 public:
  explicit MainPage(winrt::hstring const& initialUrl = {});

  winrt::Windows::UI::Xaml::UIElement Root() const { return root_; }
  void OpenExternalUrl(winrt::hstring const& url);
  void NavigateTo(winrt::hstring const& input);

 private:
  void BuildUi();
  void BuildSettingsPanel();
  void BuildExtensionsPanel();
  void BuildLogsPanel();
  void WireEvents();
  void WireSystemBack();
  void ApplySystemChrome();
  void ApplyVisibleBounds();

  void LoadFromAddressBar();
  void UpdateChrome();
  void ShowError(winrt::hstring const& title, winrt::hstring const& detail,
                 bool includeUrl = true);
  void HideError();
  void AddExtension();
  winrt::fire_and_forget AddExtensionAsync();
  void ShowSettings();
  void HideSettings();
  void ShowExtensions();
  void HideExtensions();
  void ShowLogs();
  void HideLogs();
  void SaveSettings();
  winrt::fire_and_forget LoadExtensionsAsync();
  winrt::fire_and_forget RemoveExtensionAsync(std::wstring id);
  void RefreshExtensionsList();
  void ApplyExtensionScripts();
  void PinCurrentPage();
  winrt::fire_and_forget PinCurrentPageAsync(std::wstring tileId,
                                             std::wstring display,
                                             std::wstring argumentsText);
  winrt::fire_and_forget CheckForUpdates(std::wstring url);
  bool HandleBack();
  void LaunchExternal(winrt::Windows::Foundation::Uri const& uri);

  winrt::hstring ResolveInput(winrt::hstring const& input) const;
  winrt::hstring UserAgent() const;
  void ApplyUserAgentScript();

  static winrt::hstring HomeUrl();
  static bool IsInternalScheme(winrt::hstring const& scheme);
  static winrt::hstring DescribeError(
      winrt::Windows::Web::WebErrorStatus status);

  winrt::Windows::UI::Xaml::Controls::Grid root_{nullptr};
  winrt::Windows::UI::Xaml::Controls::Grid contentHost_{nullptr};
  winrt::Windows::UI::Xaml::Controls::WebView web_{nullptr};
  winrt::Windows::UI::Xaml::Controls::TextBox addressBar_{nullptr};
  winrt::Windows::UI::Xaml::Controls::MenuFlyoutItem backItem_{nullptr};
  winrt::Windows::UI::Xaml::Controls::MenuFlyoutItem forwardItem_{nullptr};
  winrt::Windows::UI::Xaml::Controls::Button reloadButton_{nullptr};
  winrt::Windows::UI::Xaml::Controls::Button goButton_{nullptr};
  winrt::Windows::UI::Xaml::Controls::Button menuButton_{nullptr};
  winrt::Windows::UI::Xaml::Controls::Button retryButton_{nullptr};
  winrt::Windows::UI::Xaml::Controls::ProgressBar progress_{nullptr};
  winrt::Windows::UI::Xaml::Controls::Border errorPanel_{nullptr};
  winrt::Windows::UI::Xaml::Controls::TextBlock errorTitle_{nullptr};
  winrt::Windows::UI::Xaml::Controls::TextBlock errorText_{nullptr};
  winrt::Windows::UI::Xaml::Controls::TextBlock statusText_{nullptr};
  winrt::Windows::UI::Xaml::Controls::Border settingsPanel_{nullptr};
  winrt::Windows::UI::Xaml::Controls::TextBox searchBox_{nullptr};
  winrt::Windows::UI::Xaml::Controls::TextBox homeBox_{nullptr};
  winrt::Windows::UI::Xaml::Controls::TextBox userAgentBox_{nullptr};
  winrt::Windows::UI::Xaml::Controls::TextBox updateUrlBox_{nullptr};
  winrt::Windows::UI::Xaml::Controls::CheckBox logsCheckBox_{nullptr};
  winrt::Windows::UI::Xaml::Controls::StackPanel extensionsList_{nullptr};
  winrt::Windows::UI::Xaml::Controls::Border extensionsPanel_{nullptr};
  winrt::Windows::UI::Xaml::Controls::Border logsPanel_{nullptr};
  winrt::Windows::UI::Xaml::Controls::Button logsButton_{nullptr};
  winrt::Windows::UI::Xaml::Controls::ScrollViewer logsScroll_{nullptr};
  winrt::Windows::UI::Xaml::Controls::TextBlock logsText_{nullptr};

  winrt::hstring currentUrl_;
  bool extensionsLoadStarted_{false};
};

}  // namespace edgeminus
