#include "ServiceProbe.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QXmlStreamReader>
#include <chrono>

namespace wakedeck {
namespace {

ProbeStatus classify(QAbstractSocket::SocketError error) {
    switch (error) {
    case QAbstractSocket::ConnectionRefusedError: return ProbeStatus::Refused;
    case QAbstractSocket::SocketTimeoutError: return ProbeStatus::TimedOut;
    case QAbstractSocket::HostNotFoundError: return ProbeStatus::ResolveFailed;
    case QAbstractSocket::NetworkError: return ProbeStatus::Unreachable;  // no route; host or network unreachable
    case QAbstractSocket::RemoteHostClosedError: return ProbeStatus::NotReady;
    default: return ProbeStatus::Error;
    }
}

ProbeStatus classify(QNetworkReply::NetworkError error) {
    switch (error) {
    case QNetworkReply::ConnectionRefusedError: return ProbeStatus::Refused;
    case QNetworkReply::TimeoutError:
    case QNetworkReply::OperationCanceledError: return ProbeStatus::TimedOut;
    case QNetworkReply::HostNotFoundError: return ProbeStatus::ResolveFailed;
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::UnknownNetworkError: return ProbeStatus::Unreachable;
    case QNetworkReply::RemoteHostClosedError: return ProbeStatus::NotReady;
    default: return ProbeStatus::Error;
    }
}

}  // namespace

QString describe(const ProbeTarget& target) {
    if (target.kind == ProbeKind::Sunshine) {
        return QStringLiteral("Sunshine at %1:%2").arg(target.host).arg(target.port);
    }
    return QStringLiteral("TCP port %1 on %2").arg(target.port).arg(target.host);
}

// ---- TcpProbe --------------------------------------------------------------

TcpProbe::TcpProbe(const ProbeTarget& target, QObject* parent) : Probe(parent), target_(target) {
    timer_.setSingleShot(true);
    connect(&socket_, &QTcpSocket::connected, this, [this] {
        finish(ProbeStatus::Ready, QStringLiteral("port %1 accepted the connection").arg(target_.port));
    });
    connect(&socket_, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
        finish(classify(error), socket_.errorString());
    });
    connect(&timer_, &QTimer::timeout, this, [this] {
        finish(ProbeStatus::TimedOut, QStringLiteral("no answer within %1 ms").arg(target_.timeoutMs));
    });
}

void TcpProbe::start() {
    elapsed_.start();
    timer_.start(target_.timeoutMs);
    socket_.connectToHost(target_.host, target_.port);
}

void TcpProbe::finish(ProbeStatus status, const QString& detail) {
    if (done_) return;
    done_ = true;
    timer_.stop();
    socket_.abort();
    ProbeResult result;
    result.status = status;
    result.detail = detail;
    result.elapsedMs = static_cast<int>(elapsed_.elapsed());
    emit finished(result);
}

// ---- SunshineProbe ---------------------------------------------------------

SunshineProbe::SunshineProbe(const ProbeTarget& target, QObject* parent) : Probe(parent), target_(target) {}

void SunshineProbe::start() {
    elapsed_.start();

    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(target_.host);
    url.setPort(target_.port);
    url.setPath(QStringLiteral("/serverinfo"));

    QNetworkRequest request(url);
    request.setTransferTimeout(std::chrono::milliseconds(target_.timeoutMs));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);

    QNetworkReply* reply = manager_.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const QNetworkReply::NetworkError error = reply->error();
        if (error == QNetworkReply::NoError) {
            const ProbeResult parsed = interpretServerInfo(reply->readAll());
            finish(parsed.status, parsed.detail);
            return;
        }
        const ProbeStatus status = classify(error);
        if (status == ProbeStatus::TimedOut) {
            finish(status, QStringLiteral("no answer within %1 ms").arg(target_.timeoutMs));
            return;
        }
        if (status == ProbeStatus::Error) {
            const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (httpStatus > 0) {
                finish(ProbeStatus::NotReady, QStringLiteral("HTTP %1 instead of Sunshine's reply").arg(httpStatus));
                return;
            }
        }
        finish(status, reply->errorString());
    });
}

ProbeResult SunshineProbe::interpretServerInfo(const QByteArray& body) {
    ProbeResult result;
    result.status = ProbeStatus::NotReady;

    QXmlStreamReader xml(body);
    QString hostname;
    QString state;
    QString statusCode;
    bool sawRoot = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) continue;
        const QString name = xml.name().toString();
        if (!sawRoot) {
            sawRoot = true;
            if (name != QLatin1String("root")) {
                result.detail = QStringLiteral("the reply was not Sunshine's serverinfo");
                return result;
            }
            statusCode = xml.attributes().value(QStringLiteral("status_code")).toString();
            continue;
        }
        if (name == QLatin1String("hostname")) {
            hostname = xml.readElementText().trimmed();
        } else if (name == QLatin1String("state")) {
            state = xml.readElementText().trimmed();
        }
    }
    if (xml.hasError() || !sawRoot) {
        result.detail = QStringLiteral("the reply was not valid XML");
        return result;
    }
    if (!statusCode.isEmpty() && statusCode != QLatin1String("200")) {
        result.detail = QStringLiteral("Sunshine reported status_code %1").arg(statusCode);
        return result;
    }
    if (state.isEmpty()) {
        result.detail = QStringLiteral("the reply did not contain Sunshine's state");
        return result;
    }

    QString shortState = state;
    const QLatin1String prefix("SUNSHINE_SERVER_");
    if (shortState.startsWith(prefix)) shortState.remove(0, prefix.size());
    shortState = shortState.toLower();
    if (shortState == QLatin1String("busy")) {
        shortState += QStringLiteral(" (a stream is already active; Moonlight resumes it)");
    }

    result.status = ProbeStatus::Ready;
    result.detail = hostname.isEmpty() ? QStringLiteral("state %1").arg(shortState)
                                       : QStringLiteral("hostname %1, state %2").arg(hostname, shortState);
    return result;
}

void SunshineProbe::finish(ProbeStatus status, const QString& detail) {
    if (done_) return;
    done_ = true;
    ProbeResult result;
    result.status = status;
    result.detail = detail;
    result.elapsedMs = static_cast<int>(elapsed_.elapsed());
    emit finished(result);
}

Probe* makeProbe(const ProbeTarget& target, QObject* parent) {
    if (target.kind == ProbeKind::Sunshine) return new SunshineProbe(target, parent);
    return new TcpProbe(target, parent);
}

}  // namespace wakedeck
