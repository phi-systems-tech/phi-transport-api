// Tests for commandframe.h: the frame checks WS and CLI share, and the frames
// that used to throw out of nlohmann (pi5/taifun audit, 2026-10-27).

#include "phi/transport/api/commandframe.h"

#include <cstdio>
#include <string>
#include <variant>

namespace {

int g_failures = 0;

void check(bool condition, const char *what)
{
    if (condition)
        return;
    ++g_failures;
    std::printf("FAIL %s\n", what);
}

using namespace phicore::transport;

const FrameError *errorOf(const std::variant<CommandFrame, FrameError> &r)
{
    return std::get_if<FrameError>(&r);
}

void testValid()
{
    const auto r = parseCommandFrame(R"({"type":"cmd","cid":7,"topic":"  sync.ping.get \t","payload":{"a":1}})");
    const auto *frame = std::get_if<CommandFrame>(&r);
    check(frame != nullptr, "a valid frame parses");
    if (!frame)
        return;
    check(frame->cid == 7, "cid read");
    check(frame->topic == "sync.ping.get", "topic trimmed at both ends");
    check(frame->payload.value("a", 0) == 1, "payload kept");

    const auto s = parseCommandFrame(R"({"type":"cmd","cid":"12","topic":"x"})");
    const auto *f2 = std::get_if<CommandFrame>(&s);
    check(f2 && f2->cid == 12, "string cid read");
    check(f2 && f2->payload.is_object() && f2->payload.empty(), "missing payload is an empty object");

    const auto t = parseCommandFrame(R"({"type":"cmd","cid":1,"topic":"x","payload":[1,2]})");
    const auto *f3 = std::get_if<CommandFrame>(&t);
    check(f3 && f3->payload.is_object(), "a non-object payload becomes an empty object");
}

void testRefusals()
{
    const auto *e = errorOf(parseCommandFrame("not json"));
    check(e && e->code == kErrorCodeInvalidJson && !e->cid, "garbage is invalid_json");
    e = errorOf(parseCommandFrame("[1]"));
    check(e && e->code == kErrorCodeInvalidJson, "an array is invalid_json");
    e = errorOf(parseCommandFrame(R"({"type":"cmd","topic":"x"})"));
    check(e && e->code == kErrorCodeMissingCid, "no cid");
    e = errorOf(parseCommandFrame(R"({"type":"cmd","cid":-1,"topic":"x"})"));
    check(e && e->code == kErrorCodeMissingCid, "negative cid");
    e = errorOf(parseCommandFrame(R"({"type":"event","cid":3,"topic":"x"})"));
    check(e && e->code == kErrorCodeInvalidType && e->cid == CmdId{3}, "wrong type keeps the cid");
    e = errorOf(parseCommandFrame(R"({"type":"cmd","cid":3,"topic":"  "})"));
    check(e && e->code == kErrorCodeMissingTopic, "blank topic");
}

void testWrongTypesDoNotThrow()
{
    // Each of these threw nlohmann::type_error from value() before - the first
    // one before any login, from anyone who could reach the WebSocket port.
    try {
        auto *e = errorOf(parseCommandFrame(R"({"type":5,"cid":1,"topic":"x"})"));
        check(e && e->code == kErrorCodeInvalidType, "numeric type refused, not thrown");
        e = errorOf(parseCommandFrame(R"({"type":"cmd","cid":1,"topic":42})"));
        check(e && e->code == kErrorCodeMissingTopic, "numeric topic refused, not thrown");
        e = errorOf(parseCommandFrame(R"({"type":"cmd","cid":{"a":1},"topic":"x"})"));
        check(e && e->code == kErrorCodeMissingCid, "object cid refused, not thrown");
        const auto r = parseCommandFrame(R"({"type":"cmd","cid":1,"topic":"x","payload":{"clientId":7,"authToken":null}})");
        const auto *frame = std::get_if<CommandFrame>(&r);
        check(frame && stringMember(frame->payload, "clientId").empty(), "numeric clientId reads empty");
        check(frame && stringMember(frame->payload, "authToken").empty(), "null authToken reads empty");
        check(stringMember(nlohmann::json(3), "x").empty(), "stringMember of a non-object is empty");
    } catch (...) {
        check(false, "nothing a client sends may throw");
    }
}

void testCmdResponsePayload()
{
    const auto a = nlohmann::json::parse(makeCmdResponsePayload("cmd.x", R"({"ok":true})"));
    check(a.value("cmd", std::string()) == "cmd.x" && a.contains("error") && a["error"].is_null(),
          "cmd and error:null added");
    const auto b = nlohmann::json::parse(makeCmdResponsePayload("cmd.x", R"({"error":{"code":"x"}})"));
    check(b["error"].is_object(), "an error core sent is kept");
    const auto c = nlohmann::json::parse(makeCmdResponsePayload("cmd.x", "not json"));
    check(c.is_object() && c.value("cmd", std::string()) == "cmd.x", "garbage from core still answers");
}

} // namespace

int main()
{
    testValid();
    testRefusals();
    testWrongTypesDoNotThrow();
    testCmdResponsePayload();
    if (g_failures == 0) {
        std::printf("commandframe_tests: all passed\n");
        return 0;
    }
    std::printf("commandframe_tests: %d failure(s)\n", g_failures);
    return 1;
}
