#include "UrlCommand.h"

#include <QUrlQuery>

namespace wakedeck::app {

std::optional<UrlCommand> parseWakeDeckUrl(const QUrl& url, QString* error) {
    if (!url.isValid()) {
        if (error) *error = QStringLiteral("Invalid URL");
        return std::nullopt;
    }

    if (url.scheme().compare(QStringLiteral("wakedeck"), Qt::CaseInsensitive) != 0) {
        if (error) *error = QStringLiteral("Unsupported URL scheme \"%1\" (expected wakedeck:)").arg(url.scheme());
        return std::nullopt;
    }

    QString actionStr;
    QString pathStr;

    if (!url.host().isEmpty()) {
        actionStr = url.host();
        pathStr = url.path();
    } else {
        pathStr = url.path();
        while (pathStr.startsWith('/')) pathStr.remove(0, 1);
        const int slash = pathStr.indexOf('/');
        if (slash >= 0) {
            actionStr = pathStr.left(slash);
            pathStr = pathStr.mid(slash);
        } else {
            actionStr = pathStr;
            pathStr.clear();
        }
    }

    UrlAction action = UrlAction::Connect;
    if (actionStr.compare(QStringLiteral("connect"), Qt::CaseInsensitive) == 0) {
        action = UrlAction::Connect;
    } else if (actionStr.compare(QStringLiteral("check"), Qt::CaseInsensitive) == 0) {
        action = UrlAction::Check;
    } else if (actionStr.compare(QStringLiteral("wake"), Qt::CaseInsensitive) == 0) {
        action = UrlAction::Wake;
    } else if (actionStr.compare(QStringLiteral("open"), Qt::CaseInsensitive) == 0) {
        action = UrlAction::Open;
    } else {
        if (error) *error = QStringLiteral("Unknown action \"%1\" in URL").arg(actionStr);
        return std::nullopt;
    }

    while (pathStr.startsWith('/')) pathStr.remove(0, 1);
    while (pathStr.endsWith('/')) pathStr.chop(1);

    if (pathStr.contains('/')) {
        if (error) *error = QStringLiteral("Extra path segments in URL");
        return std::nullopt;
    }

    QString profile;
    if (!pathStr.isEmpty()) {
        profile = QUrl::fromPercentEncoding(pathStr.toUtf8()).trimmed();
    }

    const QUrlQuery query(url);
    const QString queryProfile = query.queryItemValue(QStringLiteral("profile"), QUrl::FullyDecoded).trimmed();

    if (profile.isEmpty() && !queryProfile.isEmpty()) {
        profile = queryProfile;
    }

    if (profile.isEmpty()) {
        if (error) *error = QStringLiteral("Missing or empty computer name in URL");
        return std::nullopt;
    }

    return UrlCommand{action, profile};
}

}  // namespace wakedeck::app
