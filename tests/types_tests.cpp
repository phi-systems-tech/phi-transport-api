// Contract tests for the log record, the error type and the scalar values they
// carry.
//
// These encode the public contract, and the adapter SDK showed what happens when
// that kind of code is not pinned: two log numberings drifted apart and every
// adapter level arrived one step off (F-36/F-39).
//
// The encoders are deterministic concatenation, so the strongest available pin is
// the emitted text itself. That is better than a round trip: a round trip only
// proves an encoder and a decoder agree with each other, and they can be wrong
// together. Here the wire shape is asserted directly, which is what the readers -
// phi-ui and the log store - actually consume.
//
// Qt-free, like the headers under test.
#include "phi/transport/api/logentry.h"
#include "phi/transport/api/scalar.h"
#include "phi/transport/api/transporttypes.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

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

void checkEqual(long long actual, long long expected, const char *what)
{
    check(actual == expected, what,
          "expected " + std::to_string(expected) + ", got " + std::to_string(actual));
}

bool contains(const std::string &haystack, const std::string &needle)
{
    return haystack.find(needle) != std::string::npos;
}

using namespace phicore::transport;

// The vocabulary the wire and phicore::adapter::sdk::LogLevel share. The header
// static_asserts the values; this repeats them from the documented table so a
// renumber has to be a deliberate edit in two places, not one.
void testLogVocabulary()
{
    checkEqual(static_cast<int>(LogLevel::Trace), 1, "Trace is 1");
    checkEqual(static_cast<int>(LogLevel::Debug), 2, "Debug is 2");
    checkEqual(static_cast<int>(LogLevel::Info), 3, "Info is 3");
    checkEqual(static_cast<int>(LogLevel::Warn), 4, "Warn is 4");
    checkEqual(static_cast<int>(LogLevel::Error), 5, "Error is 5");

    checkEqual(std::string(logLevelName(LogLevel::Trace)), "trace", "Trace name");
    checkEqual(std::string(logLevelName(LogLevel::Error)), "error", "Error name");
    checkEqual(std::string(logSourceTypeName(LogSourceType::WebSocket)), "ws", "ws source name");

    // 0..63 shared with adapters, 64..127 core-local, 0x80 the incident bit.
    check(static_cast<int>(LogCategory::Database) < 64, "Database is in the shared range");
    check(static_cast<int>(LogCategory::Transport) >= 64, "Transport is core-local");
    checkEqual(kLogIncidentFlag, 0x80, "incident flag is bit 7");
}

void testIncidentBit()
{
    const std::uint8_t plain = makeCategory(LogCategory::Network);
    const std::uint8_t incident = makeCategory(LogCategory::Network, true);

    check(!isIncident(plain), "plain category is not an incident");
    check(isIncident(incident), "incident category is flagged");
    checkEqual(baseCategory(incident), static_cast<int>(LogCategory::Network),
               "incident bit strips back to the base category");
    check(categoryEnum(incident) == LogCategory::Network, "categoryEnum ignores the incident bit");
    // The name must not change just because the entry is an incident.
    checkEqual(std::string(logCategoryName(incident)), std::string(logCategoryName(plain)),
               "incident does not rename");

    const std::uint8_t coreLocal = makeCategory(LogCategory::Transport, true);
    checkEqual(coreLocal, 64 + 0x80, "core-local category keeps its value under the flag");
    check(categoryEnum(coreLocal) == LogCategory::Transport, "core-local base survives");
}

void testScalarEncoding()
{
    checkEqual(scalarToJson(Scalar{}), "null", "absent scalar is null");
    checkEqual(scalarToJson(Scalar{true}), "true", "true");
    checkEqual(scalarToJson(Scalar{false}), "false", "false");
    checkEqual(scalarToJson(Scalar{std::int64_t(42)}), "42", "integer");
    checkEqual(scalarToJson(Scalar{std::int64_t(-7)}), "-7", "negative integer");
    // Past 2^53: a scalar routed through a double loses the low bits here.
    checkEqual(scalarToJson(Scalar{std::int64_t(9007199254740993LL)}), "9007199254740993",
               "large integer keeps every digit");
    checkEqual(scalarToJson(Scalar{93.5}), "93.5", "double");

    // Locale-independent: std::to_string would emit a comma under de_DE.
    checkEqual(scalarToJson(Scalar{0.1}), "0.1", "fractional double");

    // JSON has no NaN or Infinity; emitting one produces a frame no parser accepts.
    checkEqual(scalarToJson(Scalar{std::numeric_limits<double>::quiet_NaN()}), "null", "NaN is null");
    checkEqual(scalarToJson(Scalar{std::numeric_limits<double>::infinity()}), "null", "inf is null");

    // Strings are escaped: a placeholder value is arbitrary user data.
    checkEqual(scalarToJson(Scalar{std::string("plain")}), "\"plain\"", "string");
    checkEqual(scalarToJson(Scalar{std::string("a\"b")}), "\"a\\\"b\"", "quote is escaped");
    checkEqual(scalarToJson(Scalar{std::string("line\nbreak")}), "\"line\\nbreak\"",
               "newline is escaped");

    checkEqual(scalarListToJson({}), "[]", "empty list");
    checkEqual(scalarListToJson({Scalar{std::int64_t(1)}, Scalar{std::string("x")}, Scalar{true}}),
               "[1,\"x\",true]", "mixed list");
}

void testScalarRendering()
{
    checkEqual(scalarToText(Scalar{}), "", "absent renders empty, not \"null\"");
    checkEqual(scalarToText(Scalar{true}), "true", "bool renders");
    checkEqual(scalarToText(Scalar{std::int64_t(42)}), "42", "integer renders");
    checkEqual(scalarToText(Scalar{93.5}), "93.5", "double renders");
    // Unquoted and unescaped: this goes into a human-readable line, not onto the wire.
    checkEqual(scalarToText(Scalar{std::string("a\"b")}), "a\"b", "string renders raw");
}

// The exact emitted text for a fully populated record. Deterministic
// concatenation makes this the tightest possible pin on the wire shape.
void testLogEntryEncoding()
{
    LogEntry entry;
    entry.level = LogLevel::Warn;
    entry.category = makeCategory(LogCategory::Network, true);
    entry.message = "disk %1 at %2%";
    entry.params = {Scalar{std::string("sda")}, Scalar{std::int64_t(93)}};
    entry.ctx = "core.storage";
    entry.fields = "{\"path\":\"/var\"}";
    // Past 2^31 ms: truncated to 32 bits this lands in 1970.
    entry.tsMs = 1787212634675LL;
    entry.sourceType = LogSourceType::Cli;
    entry.sourceId = "cli";

    checkEqual(logEntryToJson(entry),
               "{\"level\":4,\"category\":131,\"message\":\"disk %1 at %2%\","
               "\"params\":[\"sda\",93],\"ctx\":\"core.storage\","
               "\"fields\":{\"path\":\"/var\"},\"tsMs\":1787212634675,"
               "\"sourceType\":4,\"sourceId\":\"cli\"}",
               "full log record");

    // The incident bit rides in the category number; losing it would show as 3.
    check(contains(logEntryToJson(entry), "\"category\":131"), "incident bit is in the category");

    // Optional members are omitted when empty, so a reader must treat absence as
    // the default rather than as zero.
    LogEntry minimal;
    minimal.message = "plain";
    checkEqual(logEntryToJson(minimal),
               "{\"level\":3,\"category\":0,\"message\":\"plain\",\"sourceType\":1}",
               "minimal log record");

    // fields is object text and is nested, not re-escaped.
    LogEntry withFields;
    withFields.message = "x";
    withFields.fields = "{\"a\":{\"b\":\"}\"}}";
    check(contains(logEntryToJson(withFields), "\"fields\":{\"a\":{\"b\":\"}\"}}"),
          "nested fields pass through unescaped");

    // A blank fields text must not emit a broken member.
    LogEntry blankFields;
    blankFields.message = "x";
    blankFields.fields = "{}";
    check(!contains(logEntryToJson(blankFields), "fields"), "an empty fields object is omitted");

    // The message is escaped: it is a translation key, but it can still contain
    // quotes from a formatted source.
    LogEntry quoted;
    quoted.message = "said \"no\"";
    check(contains(logEntryToJson(quoted), "\"message\":\"said \\\"no\\\"\""), "message is escaped");
}

void testErrorEncoding()
{
    Error err;
    err.message = "boom %1";
    err.params = {Scalar{std::int64_t(7)}};
    err.ctx = "transport plugin";
    err.level = LogLevel::Warn;
    err.category = makeCategory(LogCategory::Auth, true);
    err.flags = static_cast<std::uint16_t>(ErrorFlag::Incident);
    err.fields = "{\"k\":1}";
    err.tsMs = 1787212634675LL;
    err.sourceType = static_cast<std::uint8_t>(LogSourceType::Transport);
    err.sourceId = "ws";

    const JsonText json = errorToJson(err);
    checkEqual(json,
               "{\"message\":\"boom %1\",\"params\":[7],\"ctx\":\"transport plugin\","
               "\"level\":4,\"levelName\":\"warn\",\"category\":194,\"categoryName\":\"auth\","
               "\"flags\":1,\"flagNames\":[\"incident\"],\"fields\":{\"k\":1},"
               "\"tsMs\":1787212634675,\"sourceType\":5,\"sourceTypeName\":\"transport\","
               "\"sourceId\":\"ws\"}",
               "full error");

    // Unlike a log record an error carries the derived names, because its readers
    // render it without a category table.
    check(contains(json, "\"levelName\":\"warn\""), "levelName is emitted");
    check(contains(json, "\"categoryName\":\"auth\""), "categoryName is emitted");
    check(contains(json, "\"sourceTypeName\":\"transport\""), "sourceTypeName is emitted");
}

void testErrorFlags()
{
    checkEqual(static_cast<int>(ErrorFlag::None), 0, "None is 0");
    check(hasErrorFlag(static_cast<std::uint16_t>(ErrorFlag::Incident), ErrorFlag::Incident),
          "incident flag is detected");
    check(!hasErrorFlag(0, ErrorFlag::Incident), "no flags means no incident");

    const auto names = errorFlagNames(static_cast<std::uint16_t>(ErrorFlag::Incident));
    checkEqual(static_cast<long long>(names.size()), 1, "one flag name");
    if (!names.empty())
        checkEqual(std::string(names.front()), "incident", "flag name");
    check(errorFlagNames(0).empty(), "no flags means no names");

    // Unknown bits must be carried on the wire but must not invent a name.
    Error err;
    err.message = "x";
    err.flags = static_cast<std::uint16_t>(0x8001);
    const JsonText json = errorToJson(err);
    check(contains(json, "\"flags\":32769"), "unknown flag bits reach the wire");
    check(contains(json, "\"flagNames\":[\"incident\"]"), "unknown bits produce no extra names");
}

// Documented, not accidental: "no error" is expressed by SyncResult::error being
// an empty optional, so an Error without a message is a half-filled struct and
// encodes to nothing rather than to an error object a client cannot render.
void testErrorWithoutMessageEncodesEmpty()
{
    Error err;
    err.level = LogLevel::Warn;
    err.category = makeCategory(LogCategory::Auth, true);
    err.flags = static_cast<std::uint16_t>(ErrorFlag::Incident);
    err.fields = "{\"k\":1}";
    err.tsMs = 5;

    checkEqual(errorToJson(err), "{}", "an Error without a message encodes to {}");
    check(!isRenderableError(err), "and reports itself as not renderable");

    Error withMessage = err;
    withMessage.message = "boom";
    check(isRenderableError(withMessage), "a message makes it renderable");
}

void testSyncResultDefaults()
{
    const SyncResult result;
    check(!result.accepted, "a default SyncResult is not accepted");
    check(!result.error.has_value(), "and carries no error");
    check(result.payloadJson.empty(), "and no payload");
    // Callers treat an empty payload as {}; the helper must agree.
    check(isEmptyJsonObject(result.payloadJson), "an empty payload reads as an empty object");
}

} // namespace

int main()
{
    testLogVocabulary();
    testIncidentBit();
    testScalarEncoding();
    testScalarRendering();
    testLogEntryEncoding();
    testErrorEncoding();
    testErrorFlags();
    testErrorWithoutMessageEncodesEmpty();
    testSyncResultDefaults();

    if (g_failures == 0) {
        std::printf("types_tests: all passed\n");
        return 0;
    }
    std::printf("types_tests: %d failure(s)\n", g_failures);
    return 1;
}
