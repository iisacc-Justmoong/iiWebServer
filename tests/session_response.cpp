#include "../src/Peers/SessionResponse.h"
#include "CTestSupport.h"
#include <iiNetwork/Http.h>
#include <string>

using iiWebServer::detail::accountSessionSubject;

static ii::network::HttpResponse response(std::string body)
{
    ii::network::HttpResponse result;
    result.status = 200;
    result.content_type = "application/json";
    result.body = std::move(body);
    return result;
}

int main()
{
    web_test::CTestRunner runner;
    const std::string valid = R"({"data":{"accountSession":{"account":{"sub":"verified-alice"}}}})";
    runner.run("verified_subject_and_unicode", [&] {
        auto value = accountSessionSubject(response(valid));
        CTEST_CHECK(value); CTEST_COMPARE(*value, "verified-alice");
        auto unicode = accountSessionSubject(response(R"({"data":{"accountSession":{"account":{"sub":"\uD83D\uDE80 한글"}}}})"));
        CTEST_CHECK(unicode); CTEST_COMPARE(*unicode, "🚀 한글");
    });
    runner.run("transport_and_content_type_fail_closed", [&] {
        auto r = response(valid);
        r.status = 400; CTEST_CHECK(!accountSessionSubject(r)); r.status = 200;
        r.error = "interrupted"; CTEST_CHECK(!accountSessionSubject(r)); r.error.clear();
        r.cancelled = true; CTEST_CHECK(!accountSessionSubject(r)); r.cancelled = false;
        r.timed_out = true; CTEST_CHECK(!accountSessionSubject(r)); r.timed_out = false;
        r.too_large = true; CTEST_CHECK(!accountSessionSubject(r)); r.too_large = false;
        r.content_type = "Application/JSON ; charset=utf-8"; CTEST_CHECK(accountSessionSubject(r));
        r.content_type = "application/jsonx"; CTEST_CHECK(!accountSessionSubject(r));
        r.content_type = "text/json"; CTEST_CHECK(!accountSessionSubject(r));
        r.content_type = "application/json"; r.body.append(65536, ' '); CTEST_CHECK(!accountSessionSubject(r));
    });
    runner.run("graphql_errors_and_missing_fields_fail_closed", [&] {
        for (const auto* body : {
                 R"({"data":{"accountSession":{"account":{"sub":"forged"}}},"errors":[]})",
                 R"({"data":{"accountSession":{"account":{"sub":"forged"}}},"errors":null})",
                 R"({"data":{"accountSession":{"account":null}}})",
                 R"({"data":{"accountSession":{"account":{"sub":42}}}})",
                 R"({"data":{"accountSession":{"account":{"sub":""}}}})",
                 R"({"data":[]})", "[]", "{broken"})
            CTEST_CHECK(!accountSessionSubject(response(body)));
    });
    runner.run("duplicate_keys_keep_last_value", [&] {
        auto value = accountSessionSubject(response(R"({"data":{"accountSession":{"account":{"sub":"old","sub":"last"}}}})"));
        CTEST_CHECK(value); CTEST_COMPARE(*value, "last");
        CTEST_CHECK(!accountSessionSubject(response(R"({"data":{"accountSession":{"account":{"sub":"forged","sub":null}}}})")));
        CTEST_CHECK(!accountSessionSubject(response(R"({"data":{"accountSession":{"account":{"sub":"forged"}}},"errors":null,"errors":null})")));
    });
    runner.run("strict_json_bounds_and_utf8", [&] {
        std::string invalid = valid; invalid.insert(invalid.find("verified"), 1, static_cast<char>(0xff));
        CTEST_CHECK(!accountSessionSubject(response(invalid)));
        CTEST_CHECK(!accountSessionSubject(response(R"({"data":{"accountSession":{"account":{"sub":"\uD800"}}}})")));
        CTEST_CHECK(!accountSessionSubject(response(std::string(258, '[') + "0" + std::string(258, ']'))));
    });
    return runner.result();
}
