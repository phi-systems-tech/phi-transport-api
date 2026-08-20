// Round-trip contract tests for LogEntry and Error.
//
// These are pure functions on the public transport contract, and the adapter SDK
// showed what happens when that kind of code is not pinned: two log numberings
// drifted apart and every adapter level arrived one step off (F-36/F-39). The
// cases below are chosen for the ways a round trip silently loses information -
// a truncated 64-bit timestamp, a dropped incident bit, a default that differs
// between the struct and the decoder - not for coverage of every field.
#include "phi/transport/api/logentry.h"
#include "phi/transport/api/transporttypes.h"

#include <QJsonDocument>
#include <QJsonValue>

#include <cstdio>
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

void checkEqual(long long actual, long long expected, const char *what)
{
    check(actual == expected, what,
          "expected " + std::to_string(expected) + ", got " + std::to_string(actual));
}

void checkEqual(const QString &actual, const QString &expected, const char *what)
{
    check(actual == expected, what,
          "expected " + expected.toStdString() + ", got " + actual.toStdString());
}

using namespace phicore::transport;

// The vocabulary the wire and phicore::adapter::sdk::LogLevel share. The headers
// static_assert the values; this repeats them from the documented table so a
// renumber has to be a deliberate edit in two places, not one.
void testLogVocabulary()
{
    checkEqual(static_cast<int>(LogLevel::Trace), 1, "Trace is 1");
    checkEqual(static_cast<int>(LogLevel::Debug), 2, "Debug is 2");
    checkEqual(static_cast<int>(LogLevel::Info), 3, "Info is 3");
    checkEqual(static_cast<int>(LogLevel::Warn), 4, "Warn is 4");
    checkEqual(static_cast<int>(LogLevel::Error), 5, "Error is 5");

    checkEqual(logLevelName(LogLevel::Trace), QStringLiteral("trace"), "Trace name");
    checkEqual(logLevelName(LogLevel::Error), QStringLiteral("error"), "Error name");

    // 0..63 shared with adapters, 64..127 core-local, 0x80 the incident bit.
    check(static_cast<int>(LogCategory::Database) < 64, "Database is in the shared range");
    check(static_cast<int>(LogCategory::Transport) >= 64, "Transport is core-local");
    checkEqual(kLogIncidentFlag, 0x80, "incident flag is bit 7");
}

void testIncidentBit()
{
    const quint8 plain = makeCategory(LogCategory::Network);
    const quint8 incident = makeCategory(LogCategory::Network, true);

    check(!isIncident(plain), "plain category is not an incident");
    check(isIncident(incident), "incident category is flagged");
    checkEqual(baseCategory(incident), static_cast<int>(LogCategory::Network),
               "incident bit strips back to the base category");
    check(categoryEnum(incident) == LogCategory::Network, "categoryEnum ignores the incident bit");
    // The name must not change just because the entry is an incident.
    checkEqual(logCategoryName(incident), logCategoryName(plain), "incident does not rename");

    // A core-local category carrying the flag must survive both operations.
    const quint8 coreLocal = makeCategory(LogCategory::Transport, true);
    checkEqual(coreLocal, 64 + 0x80, "core-local category keeps its value under the flag");
    check(categoryEnum(coreLocal) == LogCategory::Transport, "core-local base survives");
}

void testLogEntryRoundTrip()
{
    LogEntry in;
    in.level = LogLevel::Warn;
    in.category = makeCategory(LogCategory::Network, true);
    in.message = "disk %1 at %2%";
    in.params = QVariantList{QVariant(qint64(42)), QVariant(93.5), QVariant(QStringLiteral("s"))};
    in.ctx = "core.storage";
    in.fields = QJsonObject{{QStringLiteral("path"), QStringLiteral("/var")}};
    // Past 2^31 ms: read back with toInt() instead of toInteger() this truncates,
    // and every log timestamp lands in 1970.
    in.tsMs = 1787212634675LL;
    in.sourceType = LogSourceType::Cli;
    in.sourceId = "cli";

    const LogEntry out = logEntryFromJson(logEntryToJson(in));

    check(out.level == in.level, "level survives");
    checkEqual(out.category, in.category, "category survives");
    check(isIncident(out.category), "incident bit survives the round trip");
    check(out.message == in.message, "message survives");
    check(out.ctx == in.ctx, "ctx survives");
    check(out.fields == in.fields, "fields survive");
    checkEqual(out.tsMs, in.tsMs, "64-bit timestamp is not truncated");
    check(out.sourceType == in.sourceType, "sourceType survives");
    check(out.sourceId == in.sourceId, "sourceId survives");

    // Params carry translation placeholders; their types must not drift, or %1
    // renders as 42.0 on one plane and 42 on the other.
    checkEqual(out.params.size(), in.params.size(), "param count survives");
    for (int i = 0; i < out.params.size() && i < in.params.size(); ++i) {
        check(out.params[i].typeId() == in.params[i].typeId(), "param type survives",
              std::string("index ") + std::to_string(i) + ": " + in.params[i].typeName()
                  + " -> " + out.params[i].typeName());
        check(out.params[i] == in.params[i], "param value survives");
    }
}

// Absent optional members must decode to something defensible rather than to
// whatever the struct happens to default to.
void testLogEntryDefaults()
{
    const LogEntry out = logEntryFromJson(QJsonObject{{QStringLiteral("message"),
                                                       QStringLiteral("x")}});
    check(out.level == LogLevel::Info, "absent level decodes to Info");
    checkEqual(out.category, static_cast<int>(LogCategory::Internal), "absent category is Internal");
    checkEqual(out.tsMs, 0, "absent tsMs is 0");
    // Deliberately not the struct's default (Core): a JSON object that names no
    // source is unknown, not core-produced.
    check(out.sourceType == LogSourceType::Unknown, "absent sourceType decodes to Unknown");
    check(LogEntry{}.sourceType == LogSourceType::Core, "the struct's own default stays Core");
}

void testErrorRoundTrip()
{
    Error in;
    in.message = QStringLiteral("boom %1");
    in.params = QVariantList{QVariant(qint64(7))};
    in.ctx = QStringLiteral("transport plugin");
    in.level = LogLevel::Warn;
    in.category = makeCategory(LogCategory::Auth, true);
    in.flags = static_cast<quint16>(ErrorFlag::Incident);
    in.fields = QJsonObject{{QStringLiteral("k"), 1}};
    in.tsMs = 1787212634675LL;
    in.sourceType = static_cast<quint8>(LogSourceType::Transport);
    in.sourceId = QStringLiteral("ws");

    const QJsonObject json = errorToJson(in);
    const Error out = errorFromJson(json);

    checkEqual(out.message, in.message, "message survives");
    checkEqual(out.ctx, in.ctx, "ctx survives");
    check(out.level == in.level, "level survives");
    checkEqual(out.category, in.category, "category survives");
    check(isIncident(out.category), "incident bit survives");
    checkEqual(out.flags, in.flags, "flags survive");
    check(out.fields == in.fields, "fields survive");
    checkEqual(out.tsMs, in.tsMs, "64-bit timestamp is not truncated");
    checkEqual(out.sourceType, in.sourceType, "sourceType survives");
    checkEqual(out.sourceId, in.sourceId, "sourceId survives");
    checkEqual(out.params.size(), in.params.size(), "param count survives");
    if (!out.params.isEmpty() && !in.params.isEmpty())
        check(out.params[0].typeId() == in.params[0].typeId(), "param type survives");

    // The *Name members are for readers of the wire; the decoder derives them
    // from the numeric values and must not depend on them being present.
    checkEqual(json.value(QStringLiteral("levelName")).toString(), QStringLiteral("warn"),
               "levelName is emitted");
    checkEqual(json.value(QStringLiteral("categoryName")).toString(), QStringLiteral("auth"),
               "categoryName is emitted");
    QJsonObject stripped = json;
    stripped.remove(QStringLiteral("levelName"));
    stripped.remove(QStringLiteral("categoryName"));
    stripped.remove(QStringLiteral("flagNames"));
    const Error fromStripped = errorFromJson(stripped);
    check(fromStripped.level == in.level, "decoding does not depend on levelName");
    checkEqual(fromStripped.category, in.category, "decoding does not depend on categoryName");
}

void testErrorFlags()
{
    checkEqual(static_cast<int>(ErrorFlag::None), 0, "None is 0");
    check(hasErrorFlag(static_cast<quint16>(ErrorFlag::Incident), ErrorFlag::Incident),
          "incident flag is detected");
    check(!hasErrorFlag(0, ErrorFlag::Incident), "no flags means no incident");

    const QStringList names = errorFlagNames(static_cast<quint16>(ErrorFlag::Incident));
    checkEqual(names.size(), 1, "one flag name");
    if (!names.isEmpty())
        checkEqual(names.first(), QStringLiteral("incident"), "flag name");
    check(errorFlagNames(0).isEmpty(), "no flags means no names");

    // Unknown bits must not invent names, and must not be lost on the way back.
    const quint16 withUnknown = static_cast<quint16>(0x8001);
    check(errorFlagNames(withUnknown).size() == 1, "unknown bits produce no names");
    Error err;
    err.message = QStringLiteral("x");
    err.flags = withUnknown;
    checkEqual(errorFromJson(errorToJson(err)).flags, withUnknown, "unknown flag bits survive");
}

// Documented, not accidental: "no error" is expressed by SyncResult::error being
// an empty optional, so an Error without a message is a half-filled struct and
// encodes to nothing rather than to an error object a client cannot render.
void testErrorWithoutMessageEncodesEmpty()
{
    Error err;
    err.level = LogLevel::Warn;
    err.category = makeCategory(LogCategory::Auth, true);
    err.flags = static_cast<quint16>(ErrorFlag::Incident);
    err.fields = QJsonObject{{QStringLiteral("k"), 1}};
    err.tsMs = 5;

    check(errorToJson(err).isEmpty(), "an Error without a message encodes to {}");
    // ... and everything else it carried goes with it. Stated here so a change
    // to this rule is a failing test rather than a silent wire change.
    check(!errorToJson(err).contains(QStringLiteral("flags")), "flags are dropped with it");
}

} // namespace

int main()
{
    testLogVocabulary();
    testIncidentBit();
    testLogEntryRoundTrip();
    testLogEntryDefaults();
    testErrorRoundTrip();
    testErrorFlags();
    testErrorWithoutMessageEncodesEmpty();

    if (g_failures == 0) {
        std::printf("types_tests: all passed\n");
        return 0;
    }
    std::printf("types_tests: %d failure(s)\n", g_failures);
    return 1;
}
