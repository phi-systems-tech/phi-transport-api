#pragma once

#include "corefacade.h"
#include "transporttypes.h"

#include <QJsonObject>
#include <QObject>
#include <QDateTime>
#include <QtPlugin>

#define PHI_TRANSPORT_INTERFACE_IID "tech.phi-systems.phi-core.TransportInterface/1.0"

namespace phicore { class TransportManager; }

namespace phicore::transport {

class TransportManager;

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
    virtual QString apiVersion() const = 0;

    // Transport lifecycle
    //
    // `config` is expected to be the resolved transport runtime config assembled
    // by phi-core from the transport-specific configuration source.
    // Transport plugins should not introduce independent config files or hidden
    // fallback sources on their own.
    virtual bool start(const QJsonObject &config, QString *errorString) = 0;
    virtual void stop() = 0;

protected:
    void writeLog(const LogEntry &entry) const
    {
        if (m_coreFacade)
            m_coreFacade->log(entry);
    }

    void writeLog(LogLevel level,
                  quint8 category,
                  const QByteArray &message,
                  const QVariantList &params = {},
                  const QByteArray &ctx = {},
                  const QJsonObject &fields = {},
                  const QByteArray &sourceId = {},
                  qint64 tsMs = 0) const
    {
        if (!m_coreFacade)
            return;

        LogEntry entry;
        entry.level = level;
        entry.category = category;
        entry.message = message;
        entry.params = params;
        entry.ctx = ctx;
        entry.fields = fields;
        entry.tsMs = tsMs > 0 ? tsMs : QDateTime::currentMSecsSinceEpoch();
        entry.sourceType = LogSourceType::Transport;
        entry.sourceId = sourceId.isEmpty() ? pluginType().toUtf8() : sourceId;
        m_coreFacade->log(entry);
    }

    // Core callback for async command completions.
    //
    // Called by phi-core's TransportManager for async submits previously accepted
    // by callCoreAsync(). Runs in the transport plugin thread.
    virtual void onCoreAsyncResult(CmdId cmdId, const QJsonObject &payload)
    {
        Q_UNUSED(cmdId);
        Q_UNUSED(payload);
    }

    // Core callback for server-side events (event.* topics).
    //
    // Called by phi-core's TransportManager when CoreApi emits topology/state
    // changes. Runs in the transport plugin thread.
    virtual void onCoreEvent(const QString &topic, const QJsonObject &payload)
    {
        Q_UNUSED(topic);
        Q_UNUSED(payload);
    }

    SyncResult callCoreSync(const QString &topic,
                            const QJsonObject &payload,
                            int timeoutMs = 1500) const
    {
        if (!m_coreFacade) {
            SyncResult result;
            result.accepted = false;
            Error error;
            error.message = QStringLiteral("Core facade is not available");
            error.ctx = QStringLiteral("transport plugin");
            result.error = error;
            return result;
        }
        return m_coreFacade->invokeSync(topic, payload, timeoutMs);
    }

    AsyncResult callCoreAsync(const QString &topic, const QJsonObject &payload) const
    {
        if (!m_coreFacade) {
            AsyncResult result;
            result.accepted = false;
            Error error;
            error.message = QStringLiteral("Core facade is not available");
            error.ctx = QStringLiteral("transport plugin");
            result.error = error;
            return result;
        }

        // Internal routing hint for the core transport manager.
        QJsonObject payloadWithHint = payload;
        payloadWithHint.insert(QStringLiteral("__phiTransportPluginType"), pluginType());
        return m_coreFacade->invokeAsync(topic, payloadWithHint);
    }

private:
    CoreFacade *m_coreFacade = nullptr;

    friend class TransportManager;
    friend class ::phicore::TransportManager;

    // Called by core transport manager before start().
    bool setCoreFacade(CoreFacade *coreFacade)
    {
        m_coreFacade = coreFacade;
        return m_coreFacade != nullptr;
    }
};

} // namespace phicore::transport

Q_DECLARE_INTERFACE(phicore::transport::TransportInterface, PHI_TRANSPORT_INTERFACE_IID)
