#pragma once

#include "logentry.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtGlobal>

#include <optional>

namespace phicore::transport {

using CmdId = quint64;

enum class ErrorFlag : quint16 {
    None     = 0,
    Incident = 1 << 0,
};

inline constexpr quint16 operator|(ErrorFlag lhs, ErrorFlag rhs)
{
    return static_cast<quint16>(lhs) | static_cast<quint16>(rhs);
}

[[nodiscard]] inline constexpr bool hasErrorFlag(quint16 flags, ErrorFlag flag)
{
    return (flags & static_cast<quint16>(flag)) != 0;
}

[[nodiscard]] inline QStringList errorFlagNames(quint16 flags)
{
    QStringList out;
    if (hasErrorFlag(flags, ErrorFlag::Incident))
        out.push_back(QStringLiteral("incident"));
    return out;
}

struct Error {
    QString message;        // English base string (translation key)
    QVariantList params;    // ordered placeholders for %1, %2, ...
    QString ctx;            // optional hint for translation engines
    LogLevel level = LogLevel::Error;
    quint8 category = static_cast<quint8>(LogCategory::Internal);
    quint16 flags = 0;
    QJsonObject fields;
    qint64 tsMs = 0;
    quint8 sourceType = static_cast<quint8>(LogSourceType::Unknown);
    QString sourceId;
};

struct SyncResult {
    bool accepted = false;
    QJsonObject payload;
    std::optional<Error> error;
};

struct AsyncResult {
    bool accepted = false;
    CmdId cmdId = 0; // internal core command id; valid when accepted=true
    std::optional<Error> error;
};

[[nodiscard]] inline QJsonObject errorToJson(const Error &error)
{
    if (error.message.isEmpty())
        return {};

    QJsonObject obj;
    obj.insert(QStringLiteral("message"), error.message);
    if (!error.params.isEmpty())
        obj.insert(QStringLiteral("params"), QJsonArray::fromVariantList(error.params));
    if (!error.ctx.isEmpty())
        obj.insert(QStringLiteral("ctx"), error.ctx);
    obj.insert(QStringLiteral("level"), static_cast<int>(error.level));
    obj.insert(QStringLiteral("levelName"), logLevelName(error.level));
    obj.insert(QStringLiteral("category"), static_cast<int>(error.category));
    obj.insert(QStringLiteral("categoryName"), logCategoryName(error.category));
    obj.insert(QStringLiteral("flags"), static_cast<int>(error.flags));
    const QStringList flagNames = errorFlagNames(error.flags);
    if (!flagNames.isEmpty())
        obj.insert(QStringLiteral("flagNames"), QJsonArray::fromStringList(flagNames));
    if (!error.fields.isEmpty())
        obj.insert(QStringLiteral("fields"), error.fields);
    if (error.tsMs > 0)
        obj.insert(QStringLiteral("tsMs"), error.tsMs);
    if (error.sourceType != static_cast<quint8>(LogSourceType::Unknown))
        obj.insert(QStringLiteral("sourceType"), static_cast<int>(error.sourceType));
    if (error.sourceType != static_cast<quint8>(LogSourceType::Unknown))
        obj.insert(QStringLiteral("sourceTypeName"),
                   logSourceTypeName(static_cast<LogSourceType>(error.sourceType)));
    if (!error.sourceId.isEmpty())
        obj.insert(QStringLiteral("sourceId"), error.sourceId);
    return obj;
}

[[nodiscard]] inline Error errorFromJson(const QJsonObject &obj)
{
    Error error;
    error.message = obj.value(QStringLiteral("message")).toString();
    if (obj.contains(QStringLiteral("params")))
        error.params = obj.value(QStringLiteral("params")).toArray().toVariantList();
    error.ctx = obj.value(QStringLiteral("ctx")).toString();
    error.level = static_cast<LogLevel>(
        obj.value(QStringLiteral("level")).toInt(static_cast<int>(LogLevel::Error)));
    error.category = static_cast<quint8>(
        obj.value(QStringLiteral("category")).toInt(static_cast<int>(LogCategory::Internal)) & 0xff);
    error.flags = static_cast<quint16>(
        obj.value(QStringLiteral("flags")).toInt(0) & 0xffff);
    error.fields = obj.value(QStringLiteral("fields")).toObject();
    error.tsMs = obj.value(QStringLiteral("tsMs")).toInteger(0);
    error.sourceType = static_cast<quint8>(
        obj.value(QStringLiteral("sourceType")).toInt(static_cast<int>(LogSourceType::Unknown)) & 0xff);
    error.sourceId = obj.value(QStringLiteral("sourceId")).toString();
    return error;
}

} // namespace phicore::transport
