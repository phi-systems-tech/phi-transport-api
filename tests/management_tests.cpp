// Contract tests for the management surface helpers (2.1.0).
//
// A transport describes itself and answers actions in JSON text it assembles
// without a JSON library; these pin the exact text, because phi-ui reads it
// with the same code that reads an adapter's actions.
#include "transportinterface.h"

#include <cstdio>
#include <string>

namespace {

int g_failures = 0;

void checkEqual(const std::string &actual, const std::string &expected, const char *what)
{
    if (actual == expected)
        return;
    ++g_failures;
    std::printf("FAIL %s: expected %s, got %s\n", what, expected.c_str(), actual.c_str());
}

using namespace phicore::transport;

void testDescriptors()
{
    checkEqual(makeActionDescriptor("sessions", "Show sessions"),
               "{\"id\":\"sessions\",\"label\":\"Show sessions\"}",
               "plain descriptor");
    checkEqual(makeActionDescriptor("kick", "Disconnect all", "Closes every client.",
                                    "\"danger\":true,\"confirm\":{\"title\":\"Sure?\"}"),
               "{\"id\":\"kick\",\"label\":\"Disconnect all\",\"description\":\"Closes every client.\","
               "\"danger\":true,\"confirm\":{\"title\":\"Sure?\"}}",
               "descriptor with extras");
}

void testDescription()
{
    checkEqual(makeManagementDescription("", {}), "{}", "empty description");
    checkEqual(makeManagementDescription("2 clients", {}), "{\"summary\":\"2 clients\"}", "summary only");
    const JsonText a = makeActionDescriptor("a", "A");
    const JsonText b = makeActionDescriptor("b", "B");
    checkEqual(makeManagementDescription("x", {a, b}),
               "{\"summary\":\"x\",\"actions\":[{\"id\":\"a\",\"label\":\"A\"},{\"id\":\"b\",\"label\":\"B\"}]}",
               "summary and actions");
    checkEqual(makeManagementDescription("", {a}),
               "{\"actions\":[{\"id\":\"a\",\"label\":\"A\"}]}",
               "actions without summary");
}

void testResults()
{
    checkEqual(makeActionResultText("Done."), "{\"status\":0,\"resultValue\":\"Done.\"}", "text result");
    checkEqual(makeActionResultText("Done.", true),
               "{\"status\":0,\"resultValue\":\"Done.\",\"reloadLayout\":true}",
               "text result with reload");
    checkEqual(makeActionResultValue("{\"text\":\"t\",\"code\":\"c\"}"),
               "{\"status\":0,\"resultValue\":{\"text\":\"t\",\"code\":\"c\"}}",
               "value result");
    checkEqual(makeActionResultValue(""), "{\"status\":0,\"resultValue\":null}", "empty value is null");
    checkEqual(makeActionResultError("No such session"),
               "{\"status\":1,\"error\":{\"message\":\"No such session\"}}",
               "error result");
    checkEqual(makeActionResultError("Not here", status::kNotSupported),
               "{\"status\":3,\"error\":{\"message\":\"Not here\"}}",
               "error result with status");
    // A "successful" error is a contradiction; it is reported as a failure.
    checkEqual(makeActionResultError("x", status::kSuccess),
               "{\"status\":1,\"error\":{\"message\":\"x\"}}",
               "error never succeeds");
}

// The defaults a transport inherits: nothing to manage, every action declined.
class Bare final : public TransportPluginBase
{
public:
    // Core reaches these as a friend; the test as a subclass.
    using TransportInterface::describeManagement;
    using TransportInterface::invokeAction;

    std::string pluginType() const override { return "bare"; }
    std::string displayName() const override { return "Bare"; }
    std::string description() const override { return ""; }
    bool start(std::string_view, std::string *) override { return true; }
    void stop() override {}
};

void testDefaults()
{
    Bare bare;
    checkEqual(bare.describeManagement(), "{}", "default description");
    if (bare.invokeAction(1, "anything", "{}")) {
        ++g_failures;
        std::printf("FAIL default invokeAction must decline\n");
    }
}

} // namespace

int main()
{
    testDescriptors();
    testDescription();
    testResults();
    testDefaults();
    if (g_failures == 0) {
        std::printf("transport_management_tests: all passed\n");
        return 0;
    }
    std::printf("transport_management_tests: %d failure(s)\n", g_failures);
    return 1;
}
