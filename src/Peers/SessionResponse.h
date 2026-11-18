#pragma once

#include <optional>
#include <string>

namespace ii::network { struct HttpResponse; }

namespace iiWebServer::detail {
// Accept only a complete, bounded account-session response. The caller retains
// responsibility for the account identity length and expiration policy.
[[nodiscard]] std::optional<std::string> accountSessionSubject(const ii::network::HttpResponse& response);
}
