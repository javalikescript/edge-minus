#pragma once

#include "pch.h"

namespace edgeminus::content {

constexpr wchar_t kContentId[] = L"edge";

std::wstring LocalPathFromUri(winrt::Windows::Foundation::Uri const& uri);
std::wstring RemoteUriFromLocalPath(std::wstring const& localPath);
std::wstring AdaptHtml(std::wstring const& html, std::wstring const& remoteUrl);

class StreamResolver
    : public winrt::implements<StreamResolver,
                               winrt::Windows::Web::IUriToStreamResolver> {
 public:
  StreamResolver();

  winrt::Windows::Foundation::IAsyncOperation<
      winrt::Windows::Storage::Streams::IInputStream>
  UriToStreamAsync(winrt::Windows::Foundation::Uri const& uri);

 private:
  winrt::Windows::Web::Http::HttpClient client_{nullptr};
};

}  // namespace edgeminus::content
