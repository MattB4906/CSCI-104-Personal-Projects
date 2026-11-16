// UrlCommand: pure parsing for the wakedeck:// URL scheme.
// Handles wakedeck://<action>/<profile> and query fallback wakedeck://<action>?profile=<name>.
#pragma once

#include <QString>
#include <QUrl>
#include <optional>

namespace wakedeck::app {

enum class UrlAction {
    Connect,
    Check,
    Wake,
    Open
};

struct UrlCommand {
    UrlAction action = UrlAction::Connect;
    QString profileName;
};

// Parses a wakedeck:// URL. Case-insensitive scheme and action.
// Decodes percent-encoded profile names (FullyDecoded) and trims whitespace.
// Rejects other schemes, unknown actions, missing/empty profile names, or extra path segments.
std::optional<UrlCommand> parseWakeDeckUrl(const QUrl& url, QString* error = nullptr);

}  // namespace wakedeck::app
