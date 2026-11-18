#include "SessionResponse.h"

#include <iiJson/Json.h>
#include <iiNetwork/Http.h>
#include <string_view>

namespace iiWebServer::detail {
namespace {
bool asciiSpace(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

bool isJsonContentType(std::string_view value) noexcept
{
    value = value.substr(0, value.find(';'));
    while (!value.empty() && asciiSpace(value.front())) value.remove_prefix(1);
    while (!value.empty() && asciiSpace(value.back())) value.remove_suffix(1);
    constexpr std::string_view expected = "application/json";
    if (value.size() != expected.size()) return false;
    for (std::size_t i = 0; i != value.size(); ++i) {
        const char c = value[i] >= 'A' && value[i] <= 'Z' ? char(value[i] - 'A' + 'a') : value[i];
        if (c != expected[i]) return false;
    }
    return true;
}
} // namespace

std::optional<std::string> accountSessionSubject(const ii::network::HttpResponse& response)
{
    constexpr std::size_t maxResponseBytes = 65536;
    if (!response.error.empty() || response.cancelled || response.timed_out || response.too_large ||
        response.status != 200 || response.body.size() > maxResponseBytes || !isJsonContentType(response.content_type))
        return std::nullopt;

    ii::json::Limits limits;
    limits.input_bytes = maxResponseBytes;
    limits.depth = 256;
    const auto envelope = ii::json::parse_with_duplicate_keys(
        response.body, ii::json::DuplicateKeyPolicy::last_wins, limits);
    if (!envelope) return std::nullopt;
    const auto object = envelope->get<ii::json::Value::Object>();
    if (!object || object->get().contains("errors")) return std::nullopt;
    const auto data = envelope->find("data");
    if (!data) return std::nullopt;
    const auto session = data->get().find("accountSession");
    if (!session) return std::nullopt;
    const auto account = session->get().find("account");
    if (!account) return std::nullopt;
    const auto subject = account->get().find("sub");
    if (!subject) return std::nullopt;
    const auto text = subject->get().get<std::string>();
    if (!text || text->get().empty()) return std::nullopt;
    return text->get();
}
} // namespace iiWebServer::detail
