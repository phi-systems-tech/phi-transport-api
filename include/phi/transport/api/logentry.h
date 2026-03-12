#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVariantList>
#include <QtGlobal>

namespace phicore::transport {

enum class LogLevel : quint8 {
    Trace = 1,
    Debug = 2,
    Info  = 3,
    Warn  = 4,
    Error = 5,
};

enum class LogCategory : quint8 {
    Internal    = 0,
    Lifecycle   = 1,
    Discovery   = 2,
    Network     = 3,
    Protocol    = 4,
    Device      = 5,
    Config      = 6,
    Performance = 7,
    Security    = 8,
    Database    = 9,

    Transport  = 64,
    Automation = 65,
    Auth       = 66,
    Storage    = 67,
    Plugin     = 68,
    Api        = 69,
    System     = 70,
};

enum class LogSourceType : quint8 {
    Unknown    = 0,
    Core       = 1,
    Adapter    = 2,
    WebSocket  = 3,
    Cli        = 4,
    Transport  = 5,
    Automation = 6,
    Database   = 7,
};

struct LogEntry
{
    LogLevel      level = LogLevel::Info;
    quint8        category = static_cast<quint8>(LogCategory::Internal);
    QByteArray    message;
    QVariantList  params;
    QByteArray    ctx;
    QJsonObject   fields;
    qint64        tsMs = 0;
    LogSourceType sourceType = LogSourceType::Core;
    QByteArray    sourceId;
};

inline constexpr quint8 kLogIncidentFlag = 0x80;

[[nodiscard]] inline constexpr bool isIncident(quint8 category)
{
    return (category & kLogIncidentFlag) != 0;
}

[[nodiscard]] inline constexpr quint8 baseCategory(quint8 category)
{
    return static_cast<quint8>(category & 0x7f);
}

[[nodiscard]] inline constexpr quint8 makeCategory(LogCategory category, bool incident = false)
{
    const quint8 value = static_cast<quint8>(category);
    return incident ? static_cast<quint8>(value | kLogIncidentFlag) : value;
}

[[nodiscard]] inline constexpr LogCategory categoryEnum(quint8 category)
{
    return static_cast<LogCategory>(category & 0x7f);
}

[[nodiscard]] inline QString logLevelName(LogLevel level)
{
    switch (level) {
    case LogLevel::Trace: return QStringLiteral("trace");
    case LogLevel::Debug: return QStringLiteral("debug");
    case LogLevel::Info: return QStringLiteral("info");
    case LogLevel::Warn: return QStringLiteral("warn");
    case LogLevel::Error: return QStringLiteral("error");
    }
    return QStringLiteral("info");
}

[[nodiscard]] inline QString logCategoryName(quint8 category)
{
    switch (categoryEnum(category)) {
    case LogCategory::Internal: return QStringLiteral("internal");
    case LogCategory::Lifecycle: return QStringLiteral("lifecycle");
    case LogCategory::Discovery: return QStringLiteral("discovery");
    case LogCategory::Network: return QStringLiteral("network");
    case LogCategory::Protocol: return QStringLiteral("protocol");
    case LogCategory::Device: return QStringLiteral("device");
    case LogCategory::Config: return QStringLiteral("config");
    case LogCategory::Performance: return QStringLiteral("performance");
    case LogCategory::Security: return QStringLiteral("security");
    case LogCategory::Database: return QStringLiteral("database");
    case LogCategory::Transport: return QStringLiteral("transport");
    case LogCategory::Automation: return QStringLiteral("automation");
    case LogCategory::Auth: return QStringLiteral("auth");
    case LogCategory::Storage: return QStringLiteral("storage");
    case LogCategory::Plugin: return QStringLiteral("plugin");
    case LogCategory::Api: return QStringLiteral("api");
    case LogCategory::System: return QStringLiteral("system");
    }
    return QStringLiteral("internal");
}

[[nodiscard]] inline QString logSourceTypeName(LogSourceType sourceType)
{
    switch (sourceType) {
    case LogSourceType::Unknown: return QStringLiteral("unknown");
    case LogSourceType::Core: return QStringLiteral("core");
    case LogSourceType::Adapter: return QStringLiteral("adapter");
    case LogSourceType::WebSocket: return QStringLiteral("ws");
    case LogSourceType::Cli: return QStringLiteral("cli");
    case LogSourceType::Transport: return QStringLiteral("transport");
    case LogSourceType::Automation: return QStringLiteral("automation");
    case LogSourceType::Database: return QStringLiteral("database");
    }
    return QStringLiteral("unknown");
}

[[nodiscard]] inline QJsonObject logEntryToJson(const LogEntry &entry)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("level"), static_cast<int>(entry.level));
    obj.insert(QStringLiteral("category"), static_cast<int>(entry.category));
    obj.insert(QStringLiteral("message"), QString::fromUtf8(entry.message));
    if (!entry.params.isEmpty())
        obj.insert(QStringLiteral("params"), QJsonArray::fromVariantList(entry.params));
    if (!entry.ctx.isEmpty())
        obj.insert(QStringLiteral("ctx"), QString::fromUtf8(entry.ctx));
    if (!entry.fields.isEmpty())
        obj.insert(QStringLiteral("fields"), entry.fields);
    if (entry.tsMs > 0)
        obj.insert(QStringLiteral("tsMs"), entry.tsMs);
    obj.insert(QStringLiteral("sourceType"), static_cast<int>(entry.sourceType));
    if (!entry.sourceId.isEmpty())
        obj.insert(QStringLiteral("sourceId"), QString::fromUtf8(entry.sourceId));
    return obj;
}

[[nodiscard]] inline LogEntry logEntryFromJson(const QJsonObject &obj)
{
    LogEntry entry;
    entry.level = static_cast<LogLevel>(
        obj.value(QStringLiteral("level")).toInt(static_cast<int>(LogLevel::Info)));
    entry.category = static_cast<quint8>(
        obj.value(QStringLiteral("category"))
            .toInt(static_cast<int>(LogCategory::Internal)) & 0xff);
    entry.message = obj.value(QStringLiteral("message")).toString().toUtf8();
    if (obj.contains(QStringLiteral("params")))
        entry.params = obj.value(QStringLiteral("params")).toArray().toVariantList();
    entry.ctx = obj.value(QStringLiteral("ctx")).toString().toUtf8();
    entry.fields = obj.value(QStringLiteral("fields")).toObject();
    entry.tsMs = obj.value(QStringLiteral("tsMs")).toInteger(0);
    entry.sourceType = static_cast<LogSourceType>(
        obj.value(QStringLiteral("sourceType")).toInt(static_cast<int>(LogSourceType::Unknown)));
    entry.sourceId = obj.value(QStringLiteral("sourceId")).toString().toUtf8();
    return entry;
}

} // namespace phicore::transport
