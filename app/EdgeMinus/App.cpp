#include "pch.h"

#include "Log.h"
#include "MainPage.h"

using namespace winrt;
using namespace winrt::Windows::ApplicationModel;
using namespace winrt::Windows::ApplicationModel::Activation;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::UI::Xaml;

namespace edgeminus {

namespace {

std::wstring PinnedUrlFromArguments(winrt::hstring const& arguments) {
  const std::wstring value(arguments.c_str());
  const std::wstring prefix = L"url|";
  if (value.rfind(prefix, 0) == 0) {
    return value.substr(prefix.size());
  }
  return {};
}

}  // namespace

struct App
    : ApplicationT<App, winrt::Windows::UI::Xaml::Markup::IXamlMetadataProvider> {
  std::shared_ptr<MainPage> page_;

  App() {
    try {
      log::Init(std::wstring(ApplicationData::Current().LocalFolder().Path()));
      log::Write(L"app: starting");
    } catch (...) {
    }

    try {
      UnhandledException(
          [](IInspectable const&, UnhandledExceptionEventArgs const& args) {
            log::Write(L"FATAL: " + std::wstring(args.Message()));
          });
    } catch (...) {
    }

    try {
      Suspending([](IInspectable const&, SuspendingEventArgs const& args) {
        log::Write(L"app: suspending");
        auto deferral = args.SuspendingOperation().GetDeferral();
        deferral.Complete();
      });
    } catch (...) {
    }

    try {
      Resuming([](IInspectable const&, IInspectable const&) {
        log::Write(L"app: resuming");
      });
    } catch (...) {
    }

    log::Write(L"app: ready");
  }

  void OnLaunched(LaunchActivatedEventArgs const& args) {
    log::Write(L"app: launched");
    const std::wstring pinned = PinnedUrlFromArguments(args.Arguments());
    EnsureContent(winrt::hstring(pinned));
    Window::Current().Activate();
  }

  void OnActivated(IActivatedEventArgs const& args) {
    std::wstring url;
    try {
      const auto kind = args.Kind();
      if (kind == ActivationKind::Protocol) {
        auto protocol = args.as<ProtocolActivatedEventArgs>();
        if (auto uri = protocol.Uri()) {
          url = uri.AbsoluteUri();
        }
      } else if (kind == ActivationKind::Launch) {
        auto launch = args.as<LaunchActivatedEventArgs>();
        url = PinnedUrlFromArguments(launch.Arguments());
      }
    } catch (winrt::hresult_error const& error) {
      log::Write(L"app: activation failed " + std::wstring(error.message()));
    }

    EnsureContent(winrt::hstring(url));
    Window::Current().Activate();
  }

  void EnsureContent(winrt::hstring const& initialUrl = {}) {
    bool created = false;
    if (!page_) {
      page_ = std::make_shared<MainPage>(initialUrl);
      created = true;
    }
    if (!Window::Current().Content()) {
      Window::Current().Content(page_->Root());
    }
    if (!created && !initialUrl.empty() && page_) {
      page_->OpenExternalUrl(initialUrl);
    }
  }

  winrt::Windows::UI::Xaml::Markup::IXamlType GetXamlType(
      winrt::Windows::UI::Xaml::Interop::TypeName const&) {
    return nullptr;
  }

  winrt::Windows::UI::Xaml::Markup::IXamlType GetXamlType(
      winrt::hstring const&) {
    return nullptr;
  }

  winrt::com_array<winrt::Windows::UI::Xaml::Markup::XmlnsDefinition>
  GetXmlnsDefinitions() {
    return {};
  }
};

}  // namespace edgeminus

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  winrt::init_apartment(winrt::apartment_type::multi_threaded);
  Application::Start(
      [](auto&&) { winrt::make<edgeminus::App>(); });
  return 0;
}
