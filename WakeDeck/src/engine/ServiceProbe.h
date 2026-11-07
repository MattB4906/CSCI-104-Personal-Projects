// ServiceProbe: asynchronous readiness checks. A Probe emits finished() exactly
// once after start(). TcpProbe checks that a port accepts connections;
// SunshineProbe asks Sunshine's HTTP endpoint for its state.
//
// Receivers must not delete a probe from inside the finished() handler; use
// deleteLater(), as Session does.
#pragma once

#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QTcpSocket>
#include <QTimer>
#include <functional>

#include "ProbeResult.h"

namespace wakedeck {

enum class ProbeKind { Tcp, Sunshine };

struct ProbeTarget {
    QString host;
    quint16 port = 47989;                  // Sunshine's HTTP port; 3389 for Remote Desktop, 22 for SSH
    ProbeKind kind = ProbeKind::Sunshine;
    int timeoutMs = 2000;
};

// "Sunshine at host:47989" or "TCP port 3389 on host".
QString describe(const ProbeTarget& target);

class Probe : public QObject {
    Q_OBJECT
public:
    explicit Probe(QObject* parent = nullptr) : QObject(parent) {}
    virtual void start() = 0;

signals:
    void finished(const wakedeck::ProbeResult& result);
};

class TcpProbe final : public Probe {
    Q_OBJECT
public:
    explicit TcpProbe(const ProbeTarget& target, QObject* parent = nullptr);
    void start() override;

private:
    void finish(ProbeStatus status, const QString& detail);

    ProbeTarget target_;
    QTcpSocket socket_;
    QTimer timer_;
    QElapsedTimer elapsed_;
    bool done_ = false;
};

class SunshineProbe final : public Probe {
    Q_OBJECT
public:
    explicit SunshineProbe(const ProbeTarget& target, QObject* parent = nullptr);
    void start() override;

    // Reads Sunshine's /serverinfo XML. Public so tests can feed it canned replies.
    static ProbeResult interpretServerInfo(const QByteArray& body);

private:
    void finish(ProbeStatus status, const QString& detail);

    ProbeTarget target_;
    QNetworkAccessManager manager_;
    QElapsedTimer elapsed_;
    bool done_ = false;
};

// Builds the probe matching target.kind. Session takes a factory of this shape
// so tests can substitute scripted probes.
using ProbeFactory = std::function<Probe*(const ProbeTarget&, QObject* parent)>;
Probe* makeProbe(const ProbeTarget& target, QObject* parent);

}  // namespace wakedeck
