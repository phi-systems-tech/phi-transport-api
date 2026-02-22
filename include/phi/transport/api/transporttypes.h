#pragma once

#include <QJsonObject>
#include <QString>
#include <QtGlobal>

#include <optional>

namespace phi::transport::api {

using RequestId = quint64;

struct ApiError {
    QString code;
    QString message;
    QJsonObject details;
};

struct SyncResult {
    bool accepted = false;
    QJsonObject payload;
    std::optional<ApiError> error;
};

} // namespace phi::transport::api
