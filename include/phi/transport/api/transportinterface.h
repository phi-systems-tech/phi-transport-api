#pragma once

#include "corefacade.h"
#include "transporttypes.h"

#include <QJsonObject>
#include <QObject>
#include <QtPlugin>

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
    virtual bool start(const QJsonObject &config, QString *errorString) = 0;
    virtual void stop() = 0;

    // Optional runtime reconfiguration.
    virtual bool reloadConfig(const QJsonObject &config, QString *errorString)
    {
        Q_UNUSED(config);
        Q_UNUSED(errorString);
        return false;
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
            Error error;
            error.msg = QStringLiteral("Core facade is not available");
            error.ctx = QStringLiteral("transport plugin");
            result.error = error;
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

#define PHI_TRANSPORT_INTERFACE_IID "tech.phi-systems.phicore-transport.TransportInterface/1.0"
Q_DECLARE_INTERFACE(phicore::transport::TransportInterface, PHI_TRANSPORT_INTERFACE_IID)
