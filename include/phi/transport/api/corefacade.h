#pragma once

#include "transporttypes.h"

#include <string_view>

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
    //  - `topic` and `payloadJson` are UTF-8: the topic as plain text, the payload
    //    as JSON object text. The data path is deliberately text - the wire is text
    //    on both ends, and it lets a transport move out of process later without
    //    its contract changing (PROTOCOLL.md 6.7).
    //  - `timeoutMs` bounds the handoff to core's thread, not the work itself:
    //    once core picks the call up it runs to completion. It must be > 0;
    //    exceeding it yields accepted=false with an error, never a silent wait.
    //  - Called from the transport plugin's own thread.
    virtual SyncResult invokeSync(std::string_view topic,
                                  std::string_view payloadJson,
                                  int timeoutMs = 1500) = 0;

    // Async call into core command routing.
    //
    // Contract:
    //  - accepted=true implies cmdId>0 and a later async result from core.
    //  - accepted=false implies no async result will follow for this submit.
    //  - "async" describes the result delivery. The submit itself reports
    //    accepted/cmdId synchronously and therefore waits for core's thread,
    //    bounded by the same budget as invokeSync's default.
    //  - `pluginType` routes the later result back to the calling transport. It is
    //    an explicit parameter, not a hidden key inside the payload.
    virtual AsyncResult invokeAsync(std::string_view topic,
                                    std::string_view payloadJson,
                                    std::string_view pluginType) = 0;
};

} // namespace phicore::transport
