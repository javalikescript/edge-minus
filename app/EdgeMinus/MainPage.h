#pragma once

#include "pch.h"

namespace edgeminus {

class MainPage {
 public:
  MainPage();

  winrt::Windows::UI::Xaml::UIElement Root() const { return root_; }
  void OpenExternalUrl(winrt::hstring const& url);
  void NavigateTo(winrt::hstring const& input);

 private:
  void BuildUi();
  void WireEvents();
  void WireSystemBack();
  void ApplySystemChrome();
  void ApplyVisibleBounds();

  void LoadFromAddressBar();
  void UpdateChrome();
  void ShowError(winrt::hstring const& title, winrt::hstring const& detail);
  void HideError();
  bool HandleBack();
  void LaunchExternal(winrt::Windows::Foundation::Uri const& uri);

  winrt::hstring ResolveInput(winrt::hstring const& input) const;

  static winrt::hstring HomeUrl();
  static bool IsInternalScheme(winrt::hstring const& scheme);
  static winrt::hstring DescribeError(
      winrt::Windows::Web::WebErrorStatus status);

  winrt::Windows::UI::Xaml::Controls::Grid root_{nullptr};
  winrt::Windows::UI::Xaml::Controls::Grid contentHost_{nullptr};
  winrt::Windows::UI::Xaml::Controls::WebView web_{nullptr};
  winrt::Windows::UI::Xaml::Controls::TextBox addressBar_{nullptr};
  winrt::Windows::UI::Xaml::Controls::Button backButton_{nullptr};
  winrt::Windows::UI::Xaml::Controls::Button forwardButton_{nullptr};
  winrt::Windows::UI::Xaml::Controls::Button reloadButton_{nullptr};
  winrt::Windows::UI::Xaml::Controls::Button goButton_{nullptr};
  winrt::Windows::UI::Xaml::Controls::Button retryButton_{nullptr};
  winrt::Windows::UI::Xaml::Controls::ProgressBar progress_{nullptr};
  winrt::Windows::UI::Xaml::Controls::Border errorPanel_{nullptr};
  winrt::Windows::UI::Xaml::Controls::TextBlock errorTitle_{nullptr};
  winrt::Windows::UI::Xaml::Controls::TextBlock errorText_{nullptr};
  winrt::Windows::UI::Xaml::Controls::TextBlock statusText_{nullptr};

  winrt::hstring currentUrl_;
};

}  // namespace edgeminus
