#pragma once

// The transport plugin contract.
//
// A transport is a shared object exporting two C entry points and returning a
// pointer to a pure abstract class. Since 2.0.0 the plugin lives on a
// phi::runtime thread core provides: every call into the plugin arrives on
// that thread's Loop, the Loop is attached before start() and stays valid
// until after stop() has returned, and the plugin drives its own I/O with the
// Loop's watches and timers. Both sides link the same libphi-runtime.so - a
// Loop& only means one thing when there is one loop state in the process.
//
// This is a source-level API, not a binary one: transports are built against the
// phi-core release they target and rebuilt for the next. That is what makes a
// C++ abstract class workable across the boundary at all - the usual objection
// is ABI fragility, which only bites when binary compatibility is promised. It
// does mean a plugin must be built with a compatible C++ toolchain; true
// language independence is the out-of-process route, not this one.
// See README ("Stability: source API, not ABI").

#include "corefacade.h"
#include "envelope.h"
#include "management.h"
#include "transporttypes.h"

#include <phi/runtime/loop.h>

#include <chrono>
#include <string>
#include <string_view>

namespace phicore {
class TransportManager;
class TransportHost;
}

namespace phicore::transport {

// The version a plugin must report from phi_transport_api_version(). phi-core
// refuses to load a transport reporting anything else - see "Version gate" in
// PROTOCOLL.md.
//
// This tracks the *interface*, not the package: it moves when the contract moves
// and stays put for a packaging, test or documentation release. Tying it to the
// package version would make every patch invalidate every installed transport,
// which is the opposite of what a source API is for.
inline constexpr const char *kTransportApiVersion = "2.1.0";

/// Symbols phi-core resolves in a transport plugin, in this order.
inline constexpr const char *kApiVersionSymbol = "phi_transport_api_version";
inline constexpr const char *kCreateSymbol = "phi_transport_create";

/**
 * @brief Transport plugin interface (pure).
 *
 * No data members at all, so the class layout is not part of the contract and
 * only the vtable is. Convenience helpers and the core facade live in
 * `TransportPluginBase`, which is compiled into the plugin alone.
 *
 * phi-core deletes the instance through this pointer, which is why the
 * destructor is virtual: the deleting destructor in the vtable runs the
 * plugin's own `operator delete`, so the allocation is freed by whoever made it.
 */
class TransportInterface
{
public:
    TransportInterface() = default;
    virtual ~TransportInterface() = default;

    TransportInterface(const TransportInterface &) = delete;
    TransportInterface &operator=(const TransportInterface &) = delete;

    /// Stable identifier; also the routing key for async results and events.
    virtual std::string pluginType() const = 0;
    virtual std::string displayName() const = 0;
    virtual std::string description() const = 0;

    // Transport lifecycle
    //
    // `configJson` is the resolved transport runtime config as UTF-8 JSON object
    // text, assembled by phi-core from the transport-specific configuration source.
    // Transport plugins should not introduce independent config files or hidden
    // fallback sources on their own.
    virtual bool start(std::string_view configJson, std::string *errorString) = 0;
    virtual void stop() = 0;

protected:
    // Core callback for async command completions.
    //
    // Called by core for async submits previously accepted by callCoreAsync() -
    // by phi-core's TransportManager in process, by phi-transport-host when the
    // transport runs as a sidecar. Runs in the transport plugin thread. `payloadJson` is
    // UTF-8 JSON object text and can be forwarded to the wire without parsing.
    virtual void onCoreAsyncResult(CmdId cmdId, std::string_view payloadJson)
    {
        (void)cmdId;
        (void)payloadJson;
    }

    // Core callback for server-side events (event.* topics).
    //
    // Called by core when CoreApi emits topology/state changes - by the
    // TransportManager or the host, as above. Runs in the transport plugin
    // thread. Core serializes the payload once for all transports; forwarding
    // it verbatim is the cheap path.
    virtual void onCoreEvent(std::string_view topic, std::string_view payloadJson)
    {
        (void)topic;
        (void)payloadJson;
    }

    // The management surface (2.1.0): what a person sees and may do about
    // this transport from the UI.
    //
    // `describeManagement()` answers with UTF-8 JSON object text - a `summary`
    // line and `actions`, in the shapes adapter actions use (management.h and
    // PROTOCOLL.md 6.4.1.6). Core asks on the plugin thread, whenever a client
    // lists the transports; keep it a read of state the plugin already has.
    // A transport with nothing to manage answers `{}`, which is the default.
    virtual JsonText describeManagement() const { return emptyJsonObject(); }

    // A person invoked one of the actions `describeManagement()` offered.
    // `paramsJson` carries the form values when the action has a form, `{}`
    // otherwise. Runs in the transport plugin thread.
    //
    // Return true to take the action: the plugin then answers, on any later
    // turn of its loop, through `CoreFacade::completeAction(cmdId, ...)` with
    // a result in the action result shape (`makeActionResult` and friends in
    // management.h). Return false to decline; core answers NotSupported on the
    // plugin's behalf and nothing further is expected. The default declines.
    virtual bool invokeAction(CmdId cmdId, std::string_view actionId, std::string_view paramsJson)
    {
        (void)cmdId;
        (void)actionId;
        (void)paramsJson;
        return false;
    }

private:
    // The two things that drive a plugin: core's TransportManager in process,
    // and phi-transport-host when the transport's config says `sidecar`. The
    // host is named here rather than reaching through the manager, because a
    // friend line is the contract's statement of who may make these calls.
    friend class ::phicore::TransportManager;
    friend class ::phicore::TransportHost;

    // Called by phi-core's transport manager before start(). Implemented once, in
    // TransportPluginBase - a plugin does not need to think about it.
    //
    // The facade is held by the plugin side rather than by this interface on
    // purpose: state here would put the class layout into the plugin contract,
    // and core would then have to agree with every plugin about its offsets.
    virtual bool attachCoreFacade(CoreFacade *coreFacade) = 0;

    // Called by phi-core's transport manager on the plugin's thread, before
    // attachCoreFacade() and start(). The loop is the plugin's to watch and
    // time against for as long as the plugin lives: every interface call
    // arrives on its thread, and the plugin must touch it only from there.
    // Implemented once, in TransportPluginBase.
    virtual void attachRuntimeLoop(phi::runtime::Loop *loop) = 0;
};

/**
 * @brief Convenience base for transport plugins.
 *
 * Holds the core facade and the helpers that use it. Everything here is compiled
 * into the plugin binary only - phi-core never sees this type, it works through
 * `TransportInterface` - so adding state to it is not a contract change.
 *
 * Deriving from it is optional; a transport may implement `TransportInterface`
 * directly, in which case it has to implement `attachCoreFacade()` and
 * `attachRuntimeLoop()` itself.
 */
class TransportPluginBase : public TransportInterface
{
public:
    TransportPluginBase() = default;
    ~TransportPluginBase() override = default;

protected:
    /// `nullptr` until phi-core has attached it, i.e. before start().
    CoreFacade *coreFacade() const { return m_coreFacade; }

    /// The plugin's loop: watches, timers and posts for the thread every
    /// interface call arrives on. `nullptr` only before core has attached it,
    /// which happens before start(). Touch it only from its own thread.
    phi::runtime::Loop *runtimeLoop() const { return m_runtimeLoop; }

    void writeLog(const LogEntry &entry) const
    {
        if (m_coreFacade)
            m_coreFacade->log(entry);
    }

    // `fields` is JSON object text, so a caller that already has serialized
    // extras passes them straight through; jsonObject()/jsonField() in
    // jsontext.h build one without a JSON library.
    void writeLog(LogLevel level,
                  std::uint8_t category,
                  std::string_view message,
                  ScalarList params = {},
                  std::string_view ctx = {},
                  std::string_view fields = {},
                  std::string_view sourceId = {},
                  std::int64_t tsMs = 0) const
    {
        if (!m_coreFacade)
            return;

        LogEntry entry;
        entry.level = level;
        entry.category = category;
        entry.message = message;
        entry.params = std::move(params);
        entry.ctx = ctx;
        entry.fields = fields;
        entry.tsMs = tsMs > 0 ? tsMs : nowMs();
        entry.sourceType = LogSourceType::Transport;
        entry.sourceId = sourceId.empty() ? pluginType() : std::string(sourceId);
        m_coreFacade->log(entry);
    }

    /**
     * @brief Route one client command and say what to answer with.
     *
     * The rule is the protocol's, not a transport's: `sync.*` is a synchronous
     * core call answered on the spot, `cmd.*` is an async submit that is acked
     * now and answered when the result arrives, anything else is an unknown
     * topic. Deciding it here is the point - two transports that each own a copy
     * of this answer differently sooner or later, which is exactly what happened
     * before (phi-core audit F-61: the CLI had grown a sync fallback for `cmd.*`
     * topics that the WS transport never had).
     *
     * There is deliberately no fallback path. A `cmd.*` topic that core does not
     * accept asynchronously is a rejected command, not an invitation to try the
     * other door.
     */
    CommandOutcome dispatchCommand(std::string_view topic,
                                   std::string_view payloadJson,
                                   const CallerIdentity &caller = {}) const
    {
        CommandOutcome outcome;

        if (topic.rfind("sync.", 0) == 0) {
            const SyncResult result = callCoreSync(topic, payloadJson, 1500, caller);
            // Accepted or not, the answer is a sync.response carrying the topic
            // it answers; only the body differs.
            const JsonText body = result.accepted
                ? result.payloadJson
                : makeSyncRejectionPayload(result.error);
            outcome.kind = CommandOutcome::Kind::SyncResponse;
            outcome.payloadJson = makeSyncResponsePayload(body, topic);
            return outcome;
        }

        if (topic.rfind("cmd.", 0) == 0) {
            const AsyncResult submitted = callCoreAsync(topic, payloadJson, caller);
            outcome.kind = CommandOutcome::Kind::Ack;
            if (submitted.accepted && submitted.cmdId > 0) {
                outcome.cmdId = submitted.cmdId;
                outcome.payloadJson = makeAckPayload(true, topic);
                return outcome;
            }
            const std::string_view message =
                (submitted.error.has_value() && !submitted.error->message.empty())
                    ? std::string_view(submitted.error->message)
                    : std::string_view("Command rejected");
            outcome.payloadJson = makeAckPayload(false, topic, message);
            return outcome;
        }

        outcome.kind = CommandOutcome::Kind::ProtocolError;
        outcome.payloadJson = makeUnknownTopicPayload(topic);
        return outcome;
    }

    SyncResult callCoreSync(std::string_view topic,
                            std::string_view payloadJson,
                            int timeoutMs = 1500,
                            const CallerIdentity &caller = {}) const
    {
        if (!m_coreFacade)
            return rejectedSync<SyncResult>();
        return m_coreFacade->invokeSync(topic, payloadJson, timeoutMs, caller);
    }

    AsyncResult callCoreAsync(std::string_view topic,
                              std::string_view payloadJson,
                              const CallerIdentity &caller = {}) const
    {
        if (!m_coreFacade)
            return rejectedSync<AsyncResult>();
        // The plugin type is a parameter, not a key smuggled through the caller's
        // payload the way it used to be (F-40). The caller identity is a
        // parameter for the same reason (F-60).
        return m_coreFacade->invokeAsync(topic, payloadJson, pluginType(), caller);
    }

    /// The answer to an action this plugin took in invokeAction(). `resultJson`
    /// is object text in the action result shape - management.h builds it.
    void completeAction(CmdId cmdId, std::string_view resultJson) const
    {
        if (m_coreFacade)
            m_coreFacade->completeAction(cmdId, resultJson);
    }

private:
    static std::int64_t nowMs()
    {
        using namespace std::chrono;
        return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    }

    template <typename Result>
    static Result rejectedSync()
    {
        Result result;
        result.accepted = false;
        Error error;
        error.message = "Core facade is not available";
        error.ctx = "transport plugin";
        result.error = error;
        return result;
    }

    bool attachCoreFacade(CoreFacade *coreFacade) override
    {
        m_coreFacade = coreFacade;
        return m_coreFacade != nullptr;
    }

    void attachRuntimeLoop(phi::runtime::Loop *loop) override
    {
        m_runtimeLoop = loop;
    }

    CoreFacade *m_coreFacade = nullptr;
    phi::runtime::Loop *m_runtimeLoop = nullptr;
};

} // namespace phicore::transport

#if defined(_WIN32)
#  define PHI_TRANSPORT_EXPORT __declspec(dllexport)
#else
#  define PHI_TRANSPORT_EXPORT __attribute__((visibility("default")))
#endif

/**
 * @brief Export the two entry points phi-core resolves.
 *
 * Place once at namespace scope in the plugin, e.g.
 * `PHI_TRANSPORT_PLUGIN(phicore::transport::ws::WsTransport)`.
 *
 * The version is a plain exported string so phi-core can read it *before*
 * constructing anything. The previous design asked the instance for its version,
 * which meant a mismatched plugin had already been instantiated - and its vtable
 * already used - by the time the answer arrived.
 */
#define PHI_TRANSPORT_PLUGIN(TypeName)                                                   \
    extern "C" PHI_TRANSPORT_EXPORT const char *phi_transport_api_version()              \
    {                                                                                    \
        return ::phicore::transport::kTransportApiVersion;                               \
    }                                                                                    \
    extern "C" PHI_TRANSPORT_EXPORT ::phicore::transport::TransportInterface              \
        *phi_transport_create()                                                          \
    {                                                                                    \
        return new TypeName();                                                           \
    }
