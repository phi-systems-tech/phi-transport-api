#pragma once

#include "transporttypes.h"

#include <QJsonObject>
#include <QString>

namespace phicore::transport {

class CoreFacade
{
public:
    virtual ~CoreFacade() = default;

    // Blocking call into core command routing.
    virtual CoreCallResult invokeSync(const QString &topic,
                                      const QJsonObject &payload,
                                      int timeoutMs = 1500) = 0;

    // Async call into core command routing.
    //
    // Contract:
    //  - accepted=true implies cmdId>0 and a later async result from core.
    //  - accepted=false implies no async result will follow for this submit.
    virtual AsyncSubmitResult invokeAsync(const QString &topic,
                                          const QJsonObject &payload) = 0;
};

} // namespace phicore::transport
