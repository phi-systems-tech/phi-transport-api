#pragma once

#include "transporttypes.h"

#include <QJsonObject>
#include <QString>

namespace phicore::transport {

class CoreFacade
{
public:
    virtual ~CoreFacade() = default;

    // Structured transport log forwarding into phi-core's logging backbone.
    virtual void log(const LogEntry &entry) = 0;

    // Blocking call into core command routing.
    //
    // Contract:
    //  - `timeoutMs` bounds the handoff to core's thread, not the work itself:
    //    once core picks the call up it runs to completion. It must be > 0;
    //    exceeding it yields accepted=false with an error, never a silent wait.
    //  - Called from the transport plugin's own thread.
    virtual SyncResult invokeSync(const QString &topic,
                                  const QJsonObject &payload,
                                  int timeoutMs = 1500) = 0;

    // Async call into core command routing.
    //
    // Contract:
    //  - accepted=true implies cmdId>0 and a later async result from core.
    //  - accepted=false implies no async result will follow for this submit.
    //  - "async" describes the result delivery. The submit itself reports
    //    accepted/cmdId synchronously and therefore waits for core's thread,
    //    bounded by the same budget as invokeSync's default.
    virtual AsyncResult invokeAsync(const QString &topic,
                                    const QJsonObject &payload) = 0;
};

} // namespace phicore::transport
