#pragma once

#include <QJsonObject>
#include <QString>
#include <QVariantList>
#include <QtGlobal>

#include <optional>

namespace phicore::transport {

using CmdId = quint64;

struct Error {
    QString msg;            // English base string (translation key)
    QVariantList params;    // ordered placeholders for %1, %2, ...
    QString ctx;            // optional hint for translation engines
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

} // namespace phicore::transport
