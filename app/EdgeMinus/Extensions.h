#pragma once

#include "pch.h"

#include <vector>

namespace edgeminus::extensions {

struct Extension {
  std::wstring id;
  std::wstring name;
  std::vector<std::wstring> patterns;
  std::wstring script;
  bool hasUserscript = false;
  bool hasIndexHtml = false;
};

class Manager {
 public:
  static Manager& Get();

  winrt::Windows::Foundation::IAsyncAction LoadAllAsync();
  winrt::Windows::Foundation::IAsyncAction LoadAsync(std::wstring const& id);
  winrt::Windows::Foundation::IAsyncAction RemoveAsync(std::wstring const& id);

  std::vector<Extension> const& All() const { return extensions_; }
  std::wstring BuildInjectionScript(std::wstring const& url) const;

 private:
  Manager() = default;

  winrt::Windows::Foundation::IAsyncAction ReadExtensionAsync(
      winrt::Windows::Storage::StorageFolder folder,
      Extension& extension) const;
  void Store(Extension extension);

  std::vector<Extension> extensions_;
};

winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> InstallAsync(
    winrt::Windows::Storage::StorageFile const& file);

bool MatchesUrl(std::wstring const& pattern, std::wstring const& url);

}  // namespace edgeminus::extensions
