// ============================================================================
// src/app/agent/DiscoAgentes.cpp — ver DiscoAgentes.hpp (fuera del hilo de la interfaz)
// ============================================================================
#include "agent/DiscoAgentes.hpp"

#include "agent/AgentViewModel.hpp"
#include "ajustes/Ajustes.hpp"
#include "persist/ColaEscritura.hpp"
#include "platform/Paths.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QUuid>
#include <QVariantMap>
#include <QVersionNumber>

#include <algorithm>

namespace ba::app::agent::disco {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

QString q(const fs::path& p) { return QString::fromStdWString(p.wstring()); }

QString sha256File(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(&f);
    return QString::fromLatin1(h.result().toHex());
}

bool writeText(const QString& path, const QByteArray& data) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(data);
    return true;
}

QString findFile(const QString& root, const QString& name) {
    QDirIterator it(root, {name}, QDir::Files | QDir::Executable, QDirIterator::Subdirectories);
    return it.hasNext() ? it.next() : QString();
}

/// El Python que trae el programa (como los plugins): ahí están zipfile y tarfile.
fs::path pythonDelPrograma() {
#if defined(_WIN32)
    return ba::platform::bundleRoot() / "python" / "python.exe";
#else
    return ba::platform::bundleRoot() / "python" / "bin" / "python3";
#endif
}

/// Descomprime .zip, .tgz/.tar.gz y .tar.xz en la carpeta pedida (sin dejar que un
/// nombre del archivo se escape de ella). Los ejecutables quedan ejecutables.
const char* kDescomprimir = R"PY(import os, sys, stat, tarfile, zipfile
src, dst = sys.argv[1], os.path.realpath(sys.argv[2])
def adentro(p):
    r = os.path.realpath(os.path.join(dst, p))
    if r != dst and not r.startswith(dst + os.sep):
        raise SystemExit("un archivo se sale de la carpeta: " + p)
    return r
if zipfile.is_zipfile(src):
    with zipfile.ZipFile(src) as z:
        for i in z.infolist():
            adentro(i.filename)
            z.extract(i, dst)
            modo = i.external_attr >> 16
            if modo & 0o111:
                p = os.path.join(dst, i.filename)
                os.chmod(p, os.stat(p).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
else:
    with tarfile.open(src) as t:
        for m in t.getmembers():
            adentro(m.name)
            if m.islnk() or m.issym():
                adentro(os.path.join(os.path.dirname(m.name), m.linkname))
        try:
            t.extractall(dst, filter="tar")
        except TypeError:
            t.extractall(dst)
)PY";

std::uintmax_t bytesDe(const fs::path& p) {
    std::error_code ec;
    if (fs::is_regular_file(p, ec)) return fs::file_size(p, ec);
    std::uintmax_t total = 0;
    for (fs::recursive_directory_iterator it(p, fs::directory_options::skip_permission_denied, ec), fin;
         !ec && it != fin; it.increment(ec)) {
        std::error_code e2;
        if (it->is_regular_file(e2)) total += it->file_size(e2);
    }
    return total;
}

/// Lo que cada motor guarda como conversaciones dentro de su perfil (relativo a
/// Credenciales/agentes/<motor>). NO está: el login (.claude.json,
/// .credentials.json, oauth_creds.json, auth.json), los ajustes, plugins y skills.
std::vector<std::string> chatsDe(const std::string& runtime) {
    if (runtime == "claude")
        return {"projects", "sessions", "session-env", "shell-snapshots", "todos", "file-history", "paste-cache",
                "debug", "backups", "history.jsonl", "statsig", "telemetry", "cache", "Library/Caches"};
    if (runtime == "antigravity")
        return {".gemini/antigravity-cli/cache", ".gemini/antigravity-cli/crashes", ".gemini/antigravity-cli/implicit",
                ".gemini/antigravity-cli/conversation_summaries.db", ".gemini/antigravity", "Library/Caches"};
    if (runtime == "pi") return {"sessions", "mcp.log", "mcp.log.1"};
    if (runtime == "opencode")
        return {"data/opencode/storage", "data/opencode/log", "data/opencode/snapshot", "state"};
    return {};
}

} // namespace

QByteArray recurso(const QString& qrc) {
    QFile f(qrc);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

json recursoJson(const QString& qrc) {
    const QByteArray b = recurso(qrc);
    return b.isEmpty() ? json::object() : json::parse(b.toStdString(), nullptr, false);
}

fs::path archivoElegidas(const fs::path& raiz) { return raiz / "Agentes" / "versiones.json"; }

json leerElegidas(const fs::path& raiz) {
    // Por la cola de escritura: si hay un cambio encolado (guardarElegidas), ese.
    std::string texto;
    if (!ba::persist::cola().leer(archivoElegidas(raiz), texto)) return json::object();
    json j = json::parse(texto, nullptr, false);
    return j.is_object() ? j : json::object();
}

Usable buscarUsable(const fs::path& raiz, const QString& runtime, const QString& fijada) {
    if (runtime.isEmpty()) return {};
    const QString base = q(raiz / "Agentes") + "/" + runtime;
    // El `.ok` guarda {version, exe} desde esta version; los viejos tenian solo
    // una fecha y para esos se cae al `exe` del pin, que es lo que habia.
    const auto leerOk = [&](const QString& dir, QString* exe) {
        QFile f(dir + "/.ok");
        if (!f.open(QIODevice::ReadOnly)) return false;
        const json j = json::parse(f.readAll().toStdString(), nullptr, false);
        if (exe != nullptr && j.is_object()) *exe = QString::fromStdString(j.value("exe", std::string()));
        return true;
    };
    const auto usable = [&](const QString& dir, const QString& exe) {
        Usable u{dir, exe};
        // pi sin jiti (instalado antes de la guarda) no carga extensiones: a reinstalar.
        u.piSinJiti = runtime == "pi" && !QFileInfo::exists(dir + "/pi/package/node_modules/jiti/package.json");
        return u;
    };
    QString exe;
    if (!fijada.isEmpty() && leerOk(base + "/" + fijada, &exe)) return usable(base + "/" + fijada, exe);
    // Quedo una instalacion de otra version (p. ej. se actualizo el sistema):
    // sirve igual, la mas nueva por orden de version, PERO SOLO DE LA MISMA VERSION
    // MAYOR que la fijada. Con otra mayor la forma de lanzarlo puede no servir (pasó:
    // opencode 1.x no entiende el --standalone de la 2 y se caía una y otra vez):
    // entonces no hay instalación usable y se pide instalar la fijada.
    const int mayorFijada = QVersionNumber::fromString(fijada).majorVersion();
    QStringList versiones = QDir(base).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    std::sort(versiones.begin(), versiones.end(), [](const QString& a, const QString& b) {
        return QVersionNumber::fromString(a) < QVersionNumber::fromString(b);
    });
    for (auto it = versiones.crbegin(); it != versiones.crend(); ++it) {
        if (!fijada.isEmpty() && QVersionNumber::fromString(*it).majorVersion() != mayorFijada) continue;
        QString e;
        if (leerOk(base + "/" + *it, &e)) return usable(base + "/" + *it, e);
    }
    return {};
}

Estado leer(const fs::path& raiz, const json& fijadas) {
    Estado e;
    e.elegidas = leerElegidas(raiz);
    for (const char* rt : kMotores) {
        json pin = fijadas.is_object() ? fijadas.value(rt, json::object()) : json::object();
        const json& el = e.elegidas.value(rt, json::object());
        if (el.is_object() && el.contains("version") && el.contains("plataformas")) pin = el;
        e.usables[rt] = buscarUsable(raiz, rt, QString::fromStdString(pin.value("version", std::string())));
    }
    return e;
}

QVariantList revisarLimpieza(const fs::path& raiz, const std::map<QString, QString>& enUso,
                             const QString& motorElegido) {
    QVariantList out;
    for (const char* rt : kMotores) {
        const fs::path prog = raiz / "Agentes" / rt;
        const fs::path perfil = raiz / "Credenciales" / "agentes" / rt;
        std::error_code ec;
        if (!fs::exists(prog, ec) && !fs::exists(perfil, ec)) continue;
        const auto u = enUso.find(rt);
        const QString enUsoDir = u == enUso.end() ? QString() : u->second;
        QVariantList versiones;
        double bytesPrograma = 0;
        for (const fs::directory_entry& d : fs::directory_iterator(prog, ec)) {
            if (!d.is_directory(ec)) continue;
            const double b = static_cast<double>(bytesDe(d.path()));
            bytesPrograma += b;
            versiones.push_back(QVariantMap{{"version", QString::fromStdString(d.path().filename().string())},
                                            {"bytes", b},
                                            {"enUso", QDir::cleanPath(q(d.path())) == QDir::cleanPath(enUsoDir)}});
        }
        double bytesChats = 0;
        for (const std::string& c : chatsDe(rt)) bytesChats += static_cast<double>(bytesDe(perfil / c));
        out.push_back(QVariantMap{{"runtime", QString(rt)},
                                  {"versiones", versiones},
                                  {"bytesPrograma", bytesPrograma},
                                  {"bytesChats", bytesChats},
                                  {"bytesPerfil", static_cast<double>(bytesDe(perfil))},
                                  {"enUso", motorElegido == rt}});
    }
    // Los espacios de trabajo (Runtime/agente, Runtime/agentes/<rol>): se rehacen al lanzar.
    const double bytesTrabajo = static_cast<double>(bytesDe(raiz / "Runtime" / "agente")) +
                                static_cast<double>(bytesDe(raiz / "Runtime" / "agentes"));
    out.push_back(QVariantMap{{"runtime", QString("espacios")}, {"bytesChats", bytesTrabajo}});
    return out;
}

double borrarChats(const fs::path& raiz, const QString& runtime) {
    double bytes = 0;
    const auto borrar = [&](const fs::path& p) {
        std::error_code ec;
        if (!fs::exists(p, ec)) return;
        bytes += static_cast<double>(bytesDe(p));
        fs::remove_all(p, ec);
    };
    for (const char* rt : kMotores) {
        if (!runtime.isEmpty() && runtime != rt) continue;
        for (const std::string& c : chatsDe(rt)) borrar(raiz / "Credenciales" / "agentes" / rt / c);
    }
    if (runtime.isEmpty()) {
        borrar(raiz / "Runtime" / "agente");
        borrar(raiz / "Runtime" / "agentes");
    }
    return bytes;
}

double borrarVersionesViejas(const fs::path& raiz, const QString& runtime, const std::map<QString, QString>& enUso,
                             QStringList& borradas) {
    double bytes = 0;
    for (const char* rt : kMotores) {
        if (!runtime.isEmpty() && runtime != rt) continue;
        const auto u = enUso.find(rt);
        const QString usada = u == enUso.end() ? QString() : QDir::cleanPath(u->second);
        if (usada.isEmpty() || usada == ".") continue; // sin una que ande no se borra nada: podría ser la única
        std::error_code ec;
        for (const fs::directory_entry& d : fs::directory_iterator(raiz / "Agentes" / rt, ec)) {
            if (!d.is_directory(ec) || QDir::cleanPath(q(d.path())) == usada) continue;
            bytes += static_cast<double>(bytesDe(d.path()));
            std::error_code e2;
            fs::remove_all(d.path(), e2);
            if (!e2) borradas << QString(rt) + " " + QString::fromStdString(d.path().filename().string());
        }
    }
    return bytes;
}

void borrarMotor(const fs::path& raiz, const QString& runtime, bool deCero) {
    std::error_code ec;
    fs::remove_all(raiz / "Agentes" / runtime.toStdString(), ec);
    if (deCero) {
        fs::remove_all(raiz / "Credenciales" / "agentes" / runtime.toStdString(), ec);
        fs::remove_all(raiz / "Runtime" / "agente", ec);
        fs::remove_all(raiz / "Runtime" / "agentes", ec);
    }
}

bool claudeGuardada(const fs::path& raiz, const QString& id) {
    if (id.isEmpty()) return false;
    QDirIterator it(q(raiz / "Credenciales" / "agentes" / "claude" / "projects"), {id + ".jsonl"}, QDir::Files,
                    QDirIterator::Subdirectories);
    return it.hasNext();
}

QString ultimaAntigravity(const fs::path& raiz, const QString& ws, qint64 desdeMs) {
    // Cada pedido queda en su historial con workspace y conversationId.
    QFile f(q(raiz / "Credenciales" / "agentes" / "antigravity" / ".gemini" / "antigravity-cli" / "history.jsonl"));
    if (!f.open(QIODevice::ReadOnly)) return {};
    f.seek(std::max<qint64>(0, f.size() - 256 * 1024));
    const QList<QByteArray> lineas = f.readAll().split('\n');
    for (auto it = lineas.crbegin(); it != lineas.crend(); ++it) {
        const json j = json::parse(it->toStdString(), nullptr, false);
        if (!j.is_object() || j.value("timestamp", qint64{0}) < desdeMs) continue;
        const QString id = QString::fromStdString(j.value("conversationId", ""));
        if (!id.isEmpty() && !QUuid::fromString(id).isNull() &&
            QDir::cleanPath(QString::fromStdString(j.value("workspace", ""))) == ws)
            return id;
    }
    return {};
}

std::string instalar(net::Descargas::Contexto& ctx, const Instalacion& in,
                     const std::function<void(const QString&)>& aviso) {
    const QString& dir = in.dir;
    const QString& staging = in.staging;
    QString err;
    QDir(staging).removeRecursively(); // restos de un intento anterior
    QDir().mkpath(staging);
    const int n = static_cast<int>(in.items.size());
    for (int i = 0; i < n && err.isEmpty(); ++i) {
        const Item& it = in.items[static_cast<std::size_t>(i)];
        const QString tmp = in.bajado + QString("/descarga-%1").arg(i);
        // Los destinos se armaron sobre `dir`: van a la carpeta de staging.
        const auto enStaging = [&dir, &staging](const std::string& p) {
            QString d = QString::fromStdString(p);
            if (d.startsWith(dir)) d.replace(0, dir.size(), staging);
            return d;
        };
        // Ya bajado y verificado en un intento anterior: no se rebaja.
        if (!QFileInfo::exists(tmp)) {
            const std::string e = ctx.bajar(it.url, tmp.toStdString(), 0, 0,
                                            QString("%1 %2 (%3/%4)").arg(in.runtime, in.version).arg(i + 1).arg(n).toStdString());
            if (!e.empty()) {
                err = ctx.cancelado() ? AgentViewModel::tr("Instalacion cancelada.")
                                      : AgentViewModel::tr("No se pudo descargar: %1").arg(QString::fromStdString(e));
                break;
            }
        }
        aviso(AgentViewModel::tr("Verificando…"));
        // SHA-256 (agentes.json, GitHub, Claude) o SHA-512 (npm). Sin
        // ninguno no se instala: nunca un archivo sin verificar.
        bool coincide = false;
        if (!it.sha.empty()) {
            coincide = sha256File(tmp) == QString::fromStdString(it.sha);
        } else if (!it.sha512.empty()) {
            QFile bajado(tmp);
            QCryptographicHash h(QCryptographicHash::Sha512);
            if (bajado.open(QIODevice::ReadOnly)) h.addData(&bajado);
            coincide = h.result().toBase64() == QByteArray::fromStdString(it.sha512);
        }
        if (!coincide) {
            err = AgentViewModel::tr("La descarga no coincide con el hash publicado. No se instalo nada.");
            QFile::remove(tmp);
            break;
        }
        if (it.kind == "binario") {
            const QString dest = enStaging(it.dest);
            QFile::remove(dest);
            if (!QFile::rename(tmp, dest)) err = AgentViewModel::tr("No se pudo guardar %1").arg(dest);
            QFile(dest).setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup |
                                       QFile::ExeGroup);
        } else {
            aviso(AgentViewModel::tr("Descomprimiendo…"));
            const bool npm = it.kind == "npm";
            const QString dest = npm ? enStaging(it.dest) + ".tmp" : enStaging(it.dest);
            QDir().mkpath(dest);
            // Con el Python DEL PROGRAMA (zipfile/tarfile): nada de afuera de la
            // carpeta del sistema (antes, el tar.exe de Windows o el tar del PATH).
            QProcess py;
            py.start(QString::fromStdString(pythonDelPrograma().string()),
                     {"-I", "-B", "-c", kDescomprimir, QDir::toNativeSeparators(tmp), QDir::toNativeSeparators(dest)});
            if (!py.waitForFinished(ba::ajustes::entero("asistente.descomprimirMin", 10) * 60000) ||
                py.exitStatus() != QProcess::NormalExit || py.exitCode() != 0)
                err = (py.error() == QProcess::FailedToStart
                           ? AgentViewModel::tr("No se pudo descomprimir: %1 (no arrancó el Python del programa)")
                           : AgentViewModel::tr("No se pudo descomprimir: %1"))
                          .arg(QString::fromUtf8(py.readAllStandardError()).trimmed().right(400));
            QFile::remove(tmp);
            if (npm && err.isEmpty()) { // <dest>.tmp/package → <dest>
                const QString final = enStaging(it.dest);
                QDir(final).removeRecursively();
                if (!QDir().rename(dest + "/package", final)) err = AgentViewModel::tr("No se pudo dejar %1").arg(final);
                QDir(dest).removeRecursively();
            }
        }
    }
    if (err.isEmpty() && !in.script.isEmpty() && !QFileInfo::exists(staging + "/" + in.runtime + "/" + in.script))
        err = AgentViewModel::tr("El paquete de %1 no trae %2: no se instalo (queda la version anterior).")
                  .arg(in.runtime, in.script);
    if (err.isEmpty()) {
        // Ejecutable en su lugar (algunos archivos lo traen en una subcarpeta).
        if (in.script.isEmpty() && !QFileInfo::exists(staging + "/" + in.exeName)) {
            const QString found = findFile(staging, in.exeName);
            if (!found.isEmpty()) QFile::rename(found, staging + "/" + in.exeName);
        }
        // El `.ok` registra que version quedo y con que ejecutable, para
        // poder seguir usandola si una version nueva del sistema fija otra.
        const json ok{{"version", QFileInfo(dir).fileName().toStdString()},
                      {"exe", in.exeName.toStdString()},
                      {"fecha", QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString()}};
        writeText(staging + "/.ok", QByteArray::fromStdString(ok.dump(2, ' ', false, json::error_handler_t::replace)));
        // Recien ahora se reemplaza lo que hubiera: hasta aca la instalacion
        // anterior siguio entera.
        QDir(dir).removeRecursively();
        if (!QDir().rename(staging, dir)) {
            QDir(staging).removeRecursively();
            err = AgentViewModel::tr("No se pudo dejar la instalacion en su lugar (%1).").arg(dir);
        }
    }
    if (!err.isEmpty()) {
        QDir(staging).removeRecursively(); // nunca se toca `dir`
    } else {
        QDir(in.bajado).removeRecursively();
        // Versiones viejas del mismo agente: afuera (liberan cientos de MB).
        QDir parent(QFileInfo(dir).absolutePath());
        for (const QString& v : parent.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            if (parent.absoluteFilePath(v) != QFileInfo(dir).absoluteFilePath())
                QDir(parent.absoluteFilePath(v)).removeRecursively();
    }
    return err.toStdString();
}

} // namespace ba::app::agent::disco
