#pragma once

#include "transporttypes.h"

#include <QJsonObject>
#include <QString>

namespace phi::transport::api {

class CoreFacade
{
public:
    virtual ~CoreFacade() = default;

    // Blocking call into core command routing.
    virtual SyncResult invokeSync(const QString &topic,
                                  const QJsonObject &payload,
                                  int timeoutMs = 1500) = 0;

    // Fire-and-forget call into core command routing.
    virtual bool invokeAsync(const QString &topic,
                             const QJsonObject &payload) = 0;
};

} // namespace phi::transport::api
