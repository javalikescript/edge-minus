#pragma once

#include "pch.h"

#include <vector>

namespace edgeminus::extensions {

struct Extension {
  std::wstring id;
  std::vector<std::wstring> patterns;
  std::vector<std::wstring> bootScripts;
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

winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> InstallFromZipAsync(
    winrt::Windows::Storage::StorageFile const& zipFile);

bool MatchesUrl(std::wstring const& pattern, std::wstring const& url);

}  // namespace edgeminus::extensions
