#pragma once

#include "transporttypes.h"

#include <cstdint>
#include <string_view>

namespace phicore::transport {

/**
 * @brief Who a call is on behalf of.
 *
 * A transport authenticates its own clients - it owns the login procedure and
 * its connections. Core authorizes: the user capabilities describe policy about
 * core's resources, and a transport gate can only answer "authenticated", never
 * "may this user manage adapters". For that, core has to know whose call it is,
 * and until now nothing crossed the boundary (phi-core audit F-60).
 *
 * It is a parameter rather than a key inside the caller's payload for the same
 * reason the plugin type became one in F-40: a hidden channel in a namespace the
 * caller owns is not a contract.
 */
struct CallerIdentity {
    enum class Kind : std::uint8_t {
        /// No identity established. Core refuses anything that needs one.
        Anonymous,
        /// A client the transport authenticated; `sessionToken` names the session.
        Session,
        /// A channel whose access is the credential - a unix socket whose
        /// permissions decide who may connect. The transport asserts this, and
        /// is responsible for it being true.
        TrustedLocal,
    };

    Kind kind = Kind::Anonymous;
    /// Session token, when kind is Session. UTF-8.
    std::string_view sessionToken;
    /// Client identifier the session was issued for, when the transport tracks one.
    std::string_view clientId;
};

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
    //  - `caller` says on whose behalf the call is made. Core decides what that
    //    identity may do; a transport that established none passes Anonymous and
    //    reaches only the pre-auth topics.
    virtual SyncResult invokeSync(std::string_view topic,
                                  std::string_view payloadJson,
                                  int timeoutMs = 1500,
                                  const CallerIdentity &caller = {}) = 0;

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
                                    std::string_view pluginType,
                                    const CallerIdentity &caller = {}) = 0;
};

} // namespace phicore::transport
