#include "CTestSupport.h"

#include <sstream>
#include <stdexcept>
#include <string>

int main()
{
    std::ostringstream errors;
    std::ostringstream output;
    auto* previous = std::cerr.rdbuf(errors.rdbuf());
    auto* previousOutput = std::cout.rdbuf(output.rdbuf());
    web_test::CTestRunner empty;
    const int emptyResult = empty.result();
    web_test::CTestRunner missing("absent");
    missing.run("other", [] { CTEST_CHECK(false); });
    const int missingResult = missing.result();
    web_test::CTestRunner failure;
    bool continued = false;
    failure.run("bad-row", [] { CTEST_CHECK(false); });
    failure.run("next-row", [&] { continued = true; });
    const int failureResult = failure.result();
    web_test::CTestRunner skipped;
    skipped.run("skip", [] { throw web_test::Skip("fixture unavailable"); });
    const int skipResult = skipped.result();
    std::cerr.rdbuf(previous);
    std::cout.rdbuf(previousOutput);
    if (emptyResult == 0 || missingResult == 0 || failureResult == 0 || skipResult != 77 || !continued ||
        errors.str().find("No test cases") == std::string::npos ||
        errors.str().find("bad-row") == std::string::npos ||
        errors.str().find("ctest_support.cpp:") == std::string::npos)
        return 1;

    web_test::CTestRunner filtered("selected");
    bool selected = false, unexpected = false;
    filtered.run("other", [&] { unexpected = true; });
    filtered.run("selected", [&] { selected = true; });
    if (!selected || unexpected || filtered.result() != 0) return 1;

    web_test::CTestRunner success;
    success.run("checks", [] {
        int evaluations = 0;
        CTEST_CHECK(true);
        CTEST_COMPARE(++evaluations, 1);
        CTEST_COMPARE(evaluations, 1);
        CTEST_THROWS(std::runtime_error, throw std::runtime_error("expected"));
    });
    return success.result();
}
