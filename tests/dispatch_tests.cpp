// Contract tests for command routing.
//
// Routing is the part the two shipped transports each used to own a copy of, and
// the part where they drifted (phi-core audit F-61). It is pinned here against a
// stub facade so the rule has one definition and one test - including the rule
// that there is no fallback: a `cmd.*` topic core will not take asynchronously
// is a rejected command, not a reason to try the sync door.
#include "phi/transport/api/transportinterface.h"

#include <cstdio>
#include <string>
#include <vector>

// The interface hands attachment rights to the transport manager alone, and that
// is right: nothing else should be able to give a plugin a core. This binary has
// no core, so the test plays that role through the same private door instead of
// widening the contract for testing.
namespace phicore {
class TransportManager
{
public:
    static bool attach(transport::TransportInterface &transport, transport::CoreFacade *facade)
    {
        return transport.attachCoreFacade(facade);
    }
};
} // namespace phicore

namespace {

int g_failures = 0;

void check(bool condition, const char *what, const std::string &detail = {})
{
    if (condition)
        return;
    ++g_failures;
    std::printf("FAIL %s%s%s\n", what, detail.empty() ? "" : ": ", detail.c_str());
}

void checkEqual(const std::string &actual, const std::string &expected, const char *what)
{
    check(actual == expected, what, "expected " + expected + ", got " + actual);
}

using namespace phicore::transport;

// Records what the plugin asked core for, and answers what the test tells it to.
class StubFacade final : public CoreFacade
{
public:
    SyncResult syncResult;
    AsyncResult asyncResult;
    std::vector<std::string> syncCalls;
    std::vector<std::string> asyncCalls;

    void log(const LogEntry &) override {}

    CallerIdentity::Kind lastCallerKind = CallerIdentity::Kind::Anonymous;
    std::string lastSessionToken;

    SyncResult invokeSync(std::string_view topic, std::string_view, int,
                          const CallerIdentity &caller) override
    {
        syncCalls.emplace_back(topic);
        lastCallerKind = caller.kind;
        lastSessionToken = std::string(caller.sessionToken);
        return syncResult;
    }

    AsyncResult invokeAsync(std::string_view topic, std::string_view, std::string_view,
                            const CallerIdentity &caller) override
    {
        asyncCalls.emplace_back(topic);
        lastCallerKind = caller.kind;
        lastSessionToken = std::string(caller.sessionToken);
        return asyncResult;
    }

    std::vector<std::pair<CmdId, std::string>> completedActions;

    void completeAction(CmdId cmdId, std::string_view resultJson) override
    {
        completedActions.emplace_back(cmdId, std::string(resultJson));
    }
};

// The smallest thing that can dispatch: no I/O, no toolkit, just the base.
class StubTransport final : public TransportPluginBase
{
public:
    std::string pluginType() const override { return "stub"; }
    std::string displayName() const override { return "Stub"; }
    std::string description() const override { return "Test double."; }
    bool start(std::string_view, std::string *) override { return true; }
    void stop() override {}

    using TransportPluginBase::dispatchCommand;

    bool attach(CoreFacade *facade)
    {
        return phicore::TransportManager::attach(*this, facade);
    }
};

void testSyncAccepted()
{
    StubFacade facade;
    facade.syncResult.accepted = true;
    facade.syncResult.payloadJson = "{\"ok\":true}";

    StubTransport transport;
    check(transport.attach(&facade), "facade attaches");

    const CommandOutcome outcome = transport.dispatchCommand("sync.hello.get", "{}");
    check(outcome.kind == CommandOutcome::Kind::SyncResponse, "sync topic answers as sync.response");
    check(outcome.cmdId == 0, "a sync answer correlates to nothing later");
    checkEqual(outcome.payloadJson, "{\"sync\":\"sync.hello.get\",\"ok\":true}",
               "core payload is spliced with the answered topic");
    check(facade.asyncCalls.empty(), "a sync topic never reaches the async path");
}

void testSyncRejected()
{
    StubFacade facade;
    facade.syncResult.accepted = false;
    Error error;
    error.message = "Permission denied";
    error.ctx = "sync.settings.get";
    facade.syncResult.error = error;

    StubTransport transport;
    transport.attach(&facade);

    const CommandOutcome outcome = transport.dispatchCommand("sync.settings.get", "{}");
    check(outcome.kind == CommandOutcome::Kind::SyncResponse,
          "a rejected sync call is still a sync.response");
    checkEqual(outcome.payloadJson,
               "{\"sync\":\"sync.settings.get\",\"accepted\":false,"
               "\"error\":{\"message\":\"Permission denied\",\"ctx\":\"sync.settings.get\"}}",
               "rejection carries message and ctx");
}

void testAsyncAccepted()
{
    StubFacade facade;
    facade.asyncResult.accepted = true;
    facade.asyncResult.cmdId = 99;

    StubTransport transport;
    transport.attach(&facade);

    const CommandOutcome outcome = transport.dispatchCommand("cmd.invokeChannel", "{}");
    check(outcome.kind == CommandOutcome::Kind::Ack, "cmd topic answers with an ack");
    check(outcome.cmdId == 99, "the accepted cmdId is handed back for the pending map");
    checkEqual(outcome.payloadJson,
               "{\"accepted\":true,\"cmd\":\"cmd.invokeChannel\",\"error\":null}",
               "accepted ack");
    check(facade.syncCalls.empty(), "an accepted cmd never touches the sync path");
}

void testAsyncRejectedHasNoFallback()
{
    StubFacade facade;
    facade.asyncResult.accepted = false;
    Error error;
    // The exact message the CLI transport used to treat as "try sync instead".
    error.message = "Unsupported async topic";
    facade.asyncResult.error = error;
    // If a fallback ever came back, this would be the answer it produced.
    facade.syncResult.accepted = true;
    facade.syncResult.payloadJson = "{\"viaFallback\":true}";

    StubTransport transport;
    transport.attach(&facade);

    const CommandOutcome outcome = transport.dispatchCommand("cmd.somethingOdd", "{}");
    check(outcome.kind == CommandOutcome::Kind::Ack, "a rejected cmd is a negative ack");
    check(outcome.cmdId == 0, "nothing to correlate");
    checkEqual(outcome.payloadJson,
               "{\"accepted\":false,\"cmd\":\"cmd.somethingOdd\","
               "\"error\":{\"code\":\"core_error\",\"message\":\"Unsupported async topic\"}}",
               "core's reason reaches the client verbatim");
    check(facade.syncCalls.empty(), "no sync fallback is attempted");
}

void testAsyncRejectedWithoutReason()
{
    StubFacade facade;
    facade.asyncResult.accepted = false;

    StubTransport transport;
    transport.attach(&facade);

    const CommandOutcome outcome = transport.dispatchCommand("cmd.x", "{}");
    checkEqual(outcome.payloadJson,
               "{\"accepted\":false,\"cmd\":\"cmd.x\","
               "\"error\":{\"code\":\"core_error\",\"message\":\"Command rejected\"}}",
               "a reasonless rejection still says something");
}

void testCallerIdentityIsForwarded()
{
    StubFacade facade;
    facade.asyncResult.accepted = true;
    facade.asyncResult.cmdId = 5;

    StubTransport transport;
    transport.attach(&facade);

    CallerIdentity caller;
    caller.kind = CallerIdentity::Kind::Session;
    caller.sessionToken = "token-abc";
    transport.dispatchCommand("cmd.invokeChannel", "{}", caller);

    // Whose call it is has to reach core, or core cannot decide what that
    // identity may do (F-60).
    check(facade.lastCallerKind == CallerIdentity::Kind::Session, "caller kind reaches core");
    check(facade.lastSessionToken == "token-abc", "session token reaches core");

    // A transport that established no identity says so rather than leaving core
    // to guess.
    transport.dispatchCommand("cmd.invokeChannel", "{}");
    check(facade.lastCallerKind == CallerIdentity::Kind::Anonymous, "default caller is anonymous");
}

void testUnknownTopic()
{
    StubFacade facade;
    StubTransport transport;
    transport.attach(&facade);

    const CommandOutcome outcome = transport.dispatchCommand("event.channel.stateChanged", "{}");
    check(outcome.kind == CommandOutcome::Kind::ProtocolError, "unroutable topic is a protocol error");
    checkEqual(outcome.payloadJson,
               "{\"code\":\"unknown_topic\","
               "\"message\":\"Unknown command topic: event.channel.stateChanged\"}",
               "the topic is echoed back");
    check(facade.syncCalls.empty() && facade.asyncCalls.empty(), "core is never called");
}

void testWithoutFacade()
{
    // Before start(), or after core detached: dispatch must not crash, and must
    // not claim the command was taken.
    StubTransport transport;
    const CommandOutcome outcome = transport.dispatchCommand("cmd.x", "{}");
    check(outcome.kind == CommandOutcome::Kind::Ack, "still an ack shape");
    check(outcome.cmdId == 0, "nothing was accepted");
    check(outcome.payloadJson.find("Core facade is not available") != std::string::npos,
          "the reason names the missing facade");
}

} // namespace

int main()
{
    testSyncAccepted();
    testSyncRejected();
    testAsyncAccepted();
    testAsyncRejectedHasNoFallback();
    testAsyncRejectedWithoutReason();
    testCallerIdentityIsForwarded();
    testUnknownTopic();
    testWithoutFacade();

    if (g_failures == 0) {
        std::printf("dispatch_tests: all passed\n");
        return 0;
    }
    std::printf("dispatch_tests: %d failure(s)\n", g_failures);
    return 1;
}
