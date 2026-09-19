// ============================================================================
// src/app/net/Credentials.hpp — Credenciales cifradas, con tipos de Qt
//
// Envoltorio fino de ba::security::CredentialStore para los clientes de
// YouTube/Twitch/Facebook y el MCP: cada credencial es `<dir>/<nombre>.cred`
// (AES-256-GCM), `dir` normalmente `Credenciales/` del bundle.
// ============================================================================
#pragma once

#include <QByteArray>
#include <QString>

#include <filesystem>
#include <string>

#include "security/CredentialStore.hpp"

namespace ba::app::cred {

inline ba::security::CredentialStore store(const QString& dir) {
    return ba::security::CredentialStore(std::filesystem::path(dir.toStdWString()));
}

inline bool read(const QString& dir, const char* name, QByteArray& out) {
    std::string s;
    if (!store(dir).read(name, s)) return false;
    out = QByteArray::fromStdString(s);
    return true;
}

inline bool write(const QString& dir, const char* name, const QByteArray& data) {
    return store(dir).write(name, data.toStdString());
}

inline void remove(const QString& dir, const char* name) { store(dir).remove(name); }

inline bool exists(const QString& dir, const char* name) { return store(dir).exists(name); }

} // namespace ba::app::cred
