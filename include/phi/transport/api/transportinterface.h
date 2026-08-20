#pragma once

#include "corefacade.h"
#include "transporttypes.h"

#include <QObject>
#include <QtPlugin>

#include <chrono>
#include <string>
#include <string_view>

// Bumped whenever the interface layout changes (a virtual added, removed or
// reordered), so Qt rejects a plugin built against an older header at load time
// instead of binding it to a vtable that no longer matches.
#define PHI_TRANSPORT_INTERFACE_IID "tech.phi-systems.phi-core.TransportInterface/1.3"

namespace phicore { class TransportManager; }

namespace phicore::transport {

// The version a plugin must report from apiVersion(). phi-core refuses to load a
// transport reporting anything else - see "Version gate" in PROTOCOLL.md. Return
// it from apiVersion() rather than hardcoding the text, so a rebuild is enough.
//
// This tracks the *interface*, not the package: it moves when the contract moves
// and stays put for a packaging, test or documentation release. Tying it to the
// package version would make every patch invalidate every installed transport,
// which is the opposite of what a source API is for.
inline constexpr const char *kTransportApiVersion = "1.3.0";

/**
 * @brief Transport plugin interface (pure).
 *
 * Deliberately stateless: it carries no data members beyond `QObject`'s own
 * (which is pimpl'd), so its layout is not part of the plugin contract and only
 * the vtable is. Convenience helpers and the core facade live in
 * `TransportPluginBase`, which is compiled into the plugin alone.
 *
 * This is a source-level API, not a binary one: transports are built against the
 * phi-core release they target and rebuilt for the next. See README
 * ("Stability: source API, not ABI").
 */
class TransportInterface : public QObject
{
public:
    explicit TransportInterface(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~TransportInterface() override = default;

    virtual QString pluginType() const = 0;
    virtual QString displayName() const = 0;
    virtual QString description() const = 0;
    /// Must return `kTransportApiVersion`; phi-core rejects any other value.
    virtual QString apiVersion() const = 0;

    // Transport lifecycle
    //
    // `configJson` is the resolved transport runtime config as UTF-8 JSON object
    // text, assembled by phi-core from the transport-specific configuration source.
    // Transport plugins should not introduce independent config files or hidden
    // fallback sources on their own.
    virtual bool start(std::string_view configJson, QString *errorString) = 0;
    virtual void stop() = 0;

protected:
    // Core callback for async command completions.
    //
    // Called by phi-core's TransportManager for async submits previously accepted
    // by callCoreAsync(). Runs in the transport plugin thread. `payloadJson` is
    // UTF-8 JSON object text and can be forwarded to the wire without parsing.
    virtual void onCoreAsyncResult(CmdId cmdId, std::string_view payloadJson)
    {
        Q_UNUSED(cmdId);
        Q_UNUSED(payloadJson);
    }

    // Core callback for server-side events (event.* topics).
    //
    // Called by phi-core's TransportManager when CoreApi emits topology/state
    // changes. Runs in the transport plugin thread. Core serializes the payload
    // once for all transports; forwarding it verbatim is the cheap path.
    virtual void onCoreEvent(std::string_view topic, std::string_view payloadJson)
    {
        Q_UNUSED(topic);
        Q_UNUSED(payloadJson);
    }

private:
    friend class ::phicore::TransportManager;

    // Called by phi-core's transport manager before start(). Implemented once, in
    // TransportPluginBase - a plugin does not need to think about it.
    //
    // The facade is held by the plugin side rather than by this interface on
    // purpose: state here would put the class layout into the plugin contract,
    // and core would then have to agree with every plugin about its offsets.
    virtual bool attachCoreFacade(CoreFacade *coreFacade) = 0;
};

/**
 * @brief Convenience base for transport plugins.
 *
 * Holds the core facade and the helpers that use it. Everything here is compiled
 * into the plugin binary only - phi-core never sees this type, it works through
 * `TransportInterface` - so adding state to it is not a contract change.
 *
 * Deriving from it is optional; a transport may implement `TransportInterface`
 * directly, in which case it has to implement `attachCoreFacade()` itself.
 */
class TransportPluginBase : public TransportInterface
{
public:
    explicit TransportPluginBase(QObject *parent = nullptr)
        : TransportInterface(parent)
    {
    }

    ~TransportPluginBase() override = default;

    QString apiVersion() const override
    {
        return QString::fromLatin1(kTransportApiVersion);
    }

protected:
    /// `nullptr` until phi-core has attached it, i.e. before start().
    CoreFacade *coreFacade() const { return m_coreFacade; }

    void writeLog(const LogEntry &entry) const
    {
        if (m_coreFacade)
            m_coreFacade->log(entry);
    }

    // `fields` is JSON object text, so a caller that already has serialized
    // extras passes them straight through; jsonField()/withJsonField() in
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
        entry.sourceId = sourceId.empty() ? pluginType().toUtf8().toStdString()
                                          : std::string(sourceId);
        m_coreFacade->log(entry);
    }

    SyncResult callCoreSync(std::string_view topic,
                            std::string_view payloadJson,
                            int timeoutMs = 1500) const
    {
        if (!m_coreFacade)
            return rejectedSync<SyncResult>();
        return m_coreFacade->invokeSync(topic, payloadJson, timeoutMs);
    }

    AsyncResult callCoreAsync(std::string_view topic, std::string_view payloadJson) const
    {
        if (!m_coreFacade)
            return rejectedSync<AsyncResult>();
        // The plugin type is a parameter, not a key smuggled through the caller's
        // payload the way it used to be (F-40).
        return m_coreFacade->invokeAsync(topic, payloadJson, pluginType().toUtf8().toStdString());
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

    CoreFacade *m_coreFacade = nullptr;
};

} // namespace phicore::transport

Q_DECLARE_INTERFACE(phicore::transport::TransportInterface, PHI_TRANSPORT_INTERFACE_IID)
