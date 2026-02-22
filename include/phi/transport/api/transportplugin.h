#pragma once

#include "corefacade.h"
#include "transporttypes.h"

#include <QJsonObject>
#include <QObject>
#include <QtPlugin>

namespace phi::transport::api {

class TransportPlugin
{
public:
    virtual ~TransportPlugin() = default;

    virtual QString pluginType() const = 0;
    virtual QString displayName() const = 0;
    virtual QString description() const = 0;
    virtual QString apiVersion() const = 0;

    // Per architecture decision, transports are singleton by default.
    virtual int maxInstances() const { return 1; }

    // Called by core before start(); plugin keeps the pointer but does not own it.
    virtual bool setCoreFacade(CoreFacade *coreFacade) = 0;

    // Transport lifecycle
    virtual bool start(const QJsonObject &config, QString *errorString) = 0;
    virtual void stop() = 0;

    // Optional runtime reconfiguration.
    virtual bool reloadConfig(const QJsonObject &config, QString *errorString)
    {
        Q_UNUSED(config);
        Q_UNUSED(errorString);
        return false;
    }
};

class TransportPluginBase : public TransportPlugin
{
public:
    bool setCoreFacade(CoreFacade *coreFacade) override
    {
        m_coreFacade = coreFacade;
        return m_coreFacade != nullptr;
    }

protected:
    CoreFacade *coreFacade() const noexcept { return m_coreFacade; }

    SyncResult callCoreSync(const QString &topic,
                            const QJsonObject &payload,
                            int timeoutMs = 1500) const
    {
        if (!m_coreFacade) {
            SyncResult result;
            result.accepted = false;
            result.error = ApiError{QStringLiteral("core_unavailable"),
                                    QStringLiteral("Core facade is not available"),
                                    QJsonObject()};
            return result;
        }
        return m_coreFacade->invokeSync(topic, payload, timeoutMs);
    }

    bool callCoreAsync(const QString &topic, const QJsonObject &payload) const
    {
        return m_coreFacade ? m_coreFacade->invokeAsync(topic, payload) : false;
    }

private:
    CoreFacade *m_coreFacade = nullptr;
};

} // namespace phi::transport::api

#define PhiTransportPlugin_iid "com.phi.transport.api.TransportPlugin/1.0"
Q_DECLARE_INTERFACE(phi::transport::api::TransportPlugin, PhiTransportPlugin_iid)
