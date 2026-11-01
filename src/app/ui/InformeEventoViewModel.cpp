// ============================================================================
// src/app/ui/InformeEventoViewModel.cpp — ver InformeEventoViewModel.hpp
// ============================================================================
#include "ui/InformeEventoViewModel.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include "evento/LineaIntercambio.hpp"
#include "i18n/I18n.hpp"
#include "persist/ColaEscritura.hpp"
#include "ui/CorridaViewModel.hpp"
#include "ui/EventoAbierto.hpp"
#include <QCoreApplication>
#include "ui/PadronViewModel.hpp"

namespace ba::app {

namespace {
using json = nlohmann::json;
QString qs(const std::string& s) { return QString::fromStdString(s); }
int64_t ahoraMs() { return QDateTime::currentMSecsSinceEpoch(); }
} // namespace

InformeEventoViewModel::InformeEventoViewModel(EventoAbierto& abierto, CorridaViewModel& corrida, PadronViewModel& padron,
                                               QObject* parent)
    : QObject(parent), ab_(abierto), corrida_(corrida), padron_(padron) {}

namespace {

/// "1:23:45" o "4:05". YouTube acepta las dos y la corta es mas legible.
QString marcaDe(int seg) {
    const int h = seg / 3600, m = (seg % 3600) / 60, s = seg % 60;
    return h > 0 ? QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'))
                 : QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}

} // namespace

QVariantMap InformeEventoViewModel::capitulos(qint64 recInicioMs) const {
    const auto fallo = [](const QString& motivo) {
        return QVariantMap{{"ok", false}, {"motivo", motivo}, {"capitulos", QVariantList{}}};
    };
    if (corrida_.corrida() == nullptr) return fallo(QCoreApplication::translate("EventoViewModel", "El guion no se corrio en esta sesion: no hay de donde sacar los tiempos."));
    const std::string salida = corrida_.salidaPorOmision();
    const auto& pasos = corrida_.corrida()->instancias();
    if (pasos.empty()) return fallo(QCoreApplication::translate("EventoViewModel", "El guion no avanzo ningun cue todavia."));
    if (recInicioMs <= 0)
        return fallo(QCoreApplication::translate("EventoViewModel", "No se cuando arranco la grabacion. Los capitulos se miden desde ahi, no desde "
                        "el arranque del evento: si el guion empezo antes, todas las marcas estarian "
                        "corridas."));

    QVariantList caps;
    int antesDeGrabar = 0;
    std::string bloqueId;
    const auto& bloques = corrida_.corrida()->linea().bloques;
    for (const evento::linea::Instancia& p : pasos) {
        if (p.salida != salida) continue;
        // Un capitulo por BLOQUE: se toma el primer paso de cada tramo.
        const evento::linea::Bloque* b = corrida_.corrida()->linea().bloqueDe(p.paso);
        if (b == nullptr || b->id == bloqueId) continue;
        bloqueId = b->id;
        const int bloqueIdx = static_cast<int>(b - bloques.data());
        const int seg = static_cast<int>((p.desdeMs - recInicioMs) / 1000);
        // Lo que paso ANTES de apretar grabar no esta en el video. Se cuenta y
        // se avisa, en vez de inventarle un 0:00 a algo que no se grabo.
        if (seg < 0) {
            ++antesDeGrabar;
            continue;
        }
        const QString nombre = b->nombre.empty() ? QCoreApplication::translate("EventoViewModel", "Bloque %1").arg(bloqueIdx + 1) : QString::fromStdString(b->nombre);
        caps.append(QVariantMap{{"seg", seg}, {"marca", marcaDe(seg)}, {"titulo", nombre}});
    }
    if (caps.isEmpty()) return fallo(QCoreApplication::translate("EventoViewModel", "Todos los bloques empezaron antes de que arrancara la grabacion."));

    // YouTube pide que el PRIMERO sea 0:00. Si el guion arranco despues de
    // apretar grabar (lo normal: se graba el ambiente antes), se antepone uno
    // para lo que haya quedado ahi adelante en vez de mover el resto.
    QVariantMap primero = caps.first().toMap();
    if (primero.value("seg").toInt() > 0)
        caps.prepend(QVariantMap{{"seg", 0}, {"marca", "0:00"}, {"titulo", QCoreApplication::translate("EventoViewModel", "Antes de empezar")}});

    // Y que ninguno dure menos de diez segundos, o los ignora a todos. Se
    // juntan los pegados en vez de devolver una lista que YouTube va a tirar.
    QVariantList limpios;
    for (const QVariant& v : caps) {
        const QVariantMap c = v.toMap();
        if (!limpios.isEmpty() && c.value("seg").toInt() - limpios.last().toMap().value("seg").toInt() < 10)
            continue;
        limpios.append(c);
    }

    QString texto;
    for (const QVariant& v : limpios) {
        const QVariantMap c = v.toMap();
        texto += c.value("marca").toString() + " " + c.value("titulo").toString() + "\n";
    }
    return QVariantMap{{"ok", limpios.size() >= 3},
                       {"motivo", limpios.size() >= 3 ? QString()
                                                      : QCoreApplication::translate("EventoViewModel", "YouTube ignora las listas de menos de tres "
                                                           "capitulos: quedaron %1.")
                                                            .arg(limpios.size())},
                       {"capitulos", limpios},
                       {"texto", texto},
                       {"antesDeGrabar", antesDeGrabar}};
}

QString InformeEventoViewModel::acta(const QJsonArray& votaciones) const {
    const evento::Resumen r = padron_.modelo().resumen();
    QString t;
    const QString titulo = ab_.nombre();
    // El acta es del evento: va en su idioma (el que fijó abrir() / setIdioma() con
    // ba::i18n::usarEnEvento; los anexos de la asamblea usan el mismo). Se arma cada vez.
    const auto T = [](const char* texto) { return qs(ba::i18n::traducirEvento("evento", texto)); };
    t += "# " + T(QT_TRANSLATE_NOOP("evento", "Acta — %1")).arg(titulo) + "\n\n";

    QString cuandoTexto = QString::fromStdString(ab_.evento().fecha);
    if (!ab_.evento().inicio.empty()) cuandoTexto += " " + QString::fromStdString(ab_.evento().inicio);
    if (!ab_.evento().huso.empty()) cuandoTexto += " (" + QString::fromStdString(ab_.evento().huso) + ")";
    t += cuandoTexto.trimmed().isEmpty() ? T(QT_TRANSLATE_NOOP("evento", "**Fecha:** sin fecha en el guion")) + "\n\n"
                                         : T(QT_TRANSLATE_NOOP("evento", "**Fecha:** %1")).arg(cuandoTexto.trimmed()) + "\n\n";

    // --- El quorum, arriba y con sus DOS numeros ---------------------------
    // Un acta que dice «se aprobo por 8 votos a favor» sin decir cuantos podian
    // votar no prueba nada.
    t += "## " + T(QT_TRANSLATE_NOOP("evento", "Asistencia")) + "\n\n";
    t += "- " +
         (r.sinDato > 0
              ? T(QT_TRANSLATE_NOOP("evento", "Presentes: **%1** de %2 en el padrón (%3 en el lugar, %4 en línea, %5 "
                                              "sin dato de por dónde)"))
                    .arg(r.presentes).arg(r.total).arg(r.presenciales).arg(r.enLinea).arg(r.sinDato)
              : T(QT_TRANSLATE_NOOP("evento", "Presentes: **%1** de %2 en el padrón (%3 en el lugar, %4 en línea)"))
                    .arg(r.presentes).arg(r.total).arg(r.presenciales).arg(r.enLinea)) +
         "\n";
    if (r.conVoto > 0)
        t += "- " +
             T(QT_TRANSLATE_NOOP("evento", "Con derecho a voto presentes: **%1** de %2 — **quórum**"))
                 .arg(r.votoPresente)
                 .arg(r.conVoto) +
             "\n";
    t += "- " + T(QT_TRANSLATE_NOOP("evento", "No llegaron: %1")).arg(r.faltan) + "\n\n";

    t += QString("| %1 | %2 | %3 | %4 | %5 |\n|---|---|---|---|---|\n")
             .arg(T(QT_TRANSLATE_NOOP("evento", "Nombre")), T(QT_TRANSLATE_NOOP("evento", "Papel")),
                  T(QT_TRANSLATE_NOOP("evento", "Por dónde")), T(QT_TRANSLATE_NOOP("evento", "Llegó")),
                  T(QT_TRANSLATE_NOOP("evento", "Voto")));
    for (const evento::Asistente& a : padron_.modelo().gente()) {
        const bool llego = a.estado == evento::Estado::Llego || a.llegoMs > 0;
        const QString hora =
            a.llegoMs > 0 ? QDateTime::fromMSecsSinceEpoch(a.llegoMs).toString("HH:mm") : QString("—");
        t += QString("| %1 | %2 | %3 | %4 | %5 |\n")
                 .arg(QString::fromStdString(a.nombre).isEmpty() ? T(QT_TRANSLATE_NOOP("evento", "(sin nombre)"))
                                                                 : QString::fromStdString(a.nombre))
                 .arg(QString::fromStdString(evento::etiquetaDePapel(a.papel)))
                 .arg(llego && !a.porDonde.empty() ? QString::fromStdString(a.porDonde)
                                                   : QString::fromStdString(evento::etiquetaDeCanal(a.canal)))
                 .arg(hora)
                 .arg(a.vota ? T(QT_TRANSLATE_NOOP("evento", "sí")) : QString("—"));
    }
    t += "\n";

    // --- Lo que se decidio --------------------------------------------------
    t += "## " + T(QT_TRANSLATE_NOOP("evento", "Lo que se votó")) + "\n\n";
    if (votaciones.isEmpty()) {
        t += T(QT_TRANSLATE_NOOP("evento", "No se votó nada en este evento.")) + "\n\n";
    } else {
        int n = 0;
        for (const QJsonValue& vv : votaciones) {
            const QJsonObject v = vv.toObject();
            ++n;
            t += QString("### %1. %2\n\n").arg(n).arg(v.value("pregunta").toString());
            const bool formal = v.value("modo").toString() == "formal";
            // Se dice CUAL fue: una encuesta leida como decision es el error
            // que esta separacion existe para evitar.
            t += "*" + (formal ? T(QT_TRANSLATE_NOOP("evento", "Votación formal"))
                               : T(QT_TRANSLATE_NOOP("evento", "Encuesta (resultado indicativo)")));
            // Lo que vota el público es una consulta: no reemplaza la votación del cuerpo.
            if (!v.value("vinculante").toBool()) t += T(QT_TRANSLATE_NOOP("evento", " — consulta, no vinculante"));
            if (v.value("secreta").toBool()) t += T(QT_TRANSLATE_NOOP("evento", ", secreta"));
            t += "*\n\n";
            const int votaron = v.value("votaron").toInt();
            const int podian = v.value("podian").toInt();
            t += (podian > 0 ? T(QT_TRANSLATE_NOOP("evento", "Votaron %1 de %2.")).arg(votaron).arg(podian)
                             : T(QT_TRANSLATE_NOOP("evento", "Votaron %1.")).arg(votaron)) +
                 "\n\n";
            for (const QJsonValue& oo : v.value("resultado").toArray()) {
                const QJsonObject o = oo.toObject();
                t +=
                    QString("- %1: **%2**\n").arg(o.value("opcion").toString()).arg(o.value("votos").toInt());
            }
            t += "\n";
            const QJsonArray faltan = v.value("faltanVotar").toArray();
            if (!faltan.isEmpty()) {
                QStringList ns;
                for (const QJsonValue& f : faltan)
                    ns << f.toString();
                t += T(QT_TRANSLATE_NOOP("evento", "No votaron: %1")).arg(ns.join(", ")) + "\n\n";
            }
        }
    }

    if (anexoActa_) t += anexoActa_();
    t += "---\n\n_" +
         T(QT_TRANSLATE_NOOP("evento", "Generada por Broadcast Appliance el %1."))
             .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm")) +
         "_\n";
    return t;
}

QString InformeEventoViewModel::guardarActa(const QString& texto) const {
    if (!ab_.hay()) return {};
    const QString nombre = "acta-" + QDateTime::currentDateTime().toString("yyyy-MM-dd-HHmm") + ".md";
    const std::filesystem::path archivo = ab_.dir() / nombre.toStdString();
    // Tiene valor legal: el escritor único, entero o nada y bajado al disco hasta su caché
    // (Critica), en su hilo (no traba la interfaz). Primero el acta y después su huella
    // (SHA-256), al lado: cualquiera puede verificar que el texto no cambió
    // (`shasum -a 256 -c acta-….md.sha256`). Si el disco falla, se reintenta y va al registro.
    const ba::persist::Pedido critico{ba::persist::Durabilidad::Critica, ba::persist::Prioridad::Normal,
                                      ba::persist::Permisos::Comunes, {}};
    ba::persist::cola().escribir(archivo, texto.toUtf8().toStdString(), critico);
    std::filesystem::path huella = archivo;
    huella += ".sha256";
    ba::persist::cola().escribir(huella, (hashActa(texto) + "  " + nombre + "\n").toUtf8().toStdString(), critico);
    return QString::fromStdString(archivo.string());
}

QString InformeEventoViewModel::hashActa(const QString& texto) {
    return QString::fromLatin1(QCryptographicHash::hash(texto.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QVariantMap InformeEventoViewModel::informe() const {
    // Un renglón por paso que salió (lo emitido de la salida por omisión).
    if (corrida_.corrida() == nullptr) return {};
    const std::string salida = corrida_.salidaPorOmision();
    const auto& pasos = corrida_.corrida()->instancias();
    if (pasos.empty()) return {};

    // Se agrupa por bloque respetando el ORDEN EN QUE PASO, no el del guion:
    // si el operador salto o repitio, el informe tiene que contarlo.
    QVariantList bloques;
    QVariantMap bloqueActual;
    QVariantList cuesDelBloque;
    std::string bloqueId;
    bool hayBloque = false;
    int bPrevisto = 0;
    int bReal = 0;
    int totalPrevisto = 0;
    int totalReal = 0;
    int nro = 0;

    const auto cerrarBloque = [&]() {
        if (!hayBloque) return;
        bloqueActual["cues"] = cuesDelBloque;
        bloqueActual["previstoSeg"] = bPrevisto;
        bloqueActual["realSeg"] = bReal;
        bloqueActual["desvioSeg"] = bReal - bPrevisto;
        bloques.append(bloqueActual);
        cuesDelBloque.clear();
        bPrevisto = 0;
        bReal = 0;
    };

    for (const evento::linea::Instancia& p : pasos) {
        // El último paso puede seguir al aire: no tiene duración real todavía.
        if (p.salida != salida || p.hastaMs == 0) continue;
        const evento::linea::Bloque* b = corrida_.corrida()->linea().bloqueDe(p.paso);
        const std::string id = b ? b->id : std::string();
        if (!hayBloque || id != bloqueId) {
            cerrarBloque();
            hayBloque = true;
            bloqueId = id;
            bloqueActual = QVariantMap{{"nombre", b ? QString::fromStdString(b->nombre) : QCoreApplication::translate("EventoViewModel", "(bloque borrado)")}};
        }
        const evento::linea::Paso* paso = corrida_.corrida()->linea().paso(p.paso);
        // El tipo es el del cue con que empieza el paso en el guion de cues.
        const auto [cb, cc] = ab_.proyeccion().cueDe(p.paso);
        const evento::Cue* c = ab_.cue(cb, cc);
        const int previsto = paso ? static_cast<int>(paso->duracionPlanMs() / 1000) : 0;
        const int real = static_cast<int>((p.hastaMs - p.desdeMs) / 1000);
        cuesDelBloque.append(QVariantMap{
            {"nro", ++nro},
            {"paso", QString::fromStdString(p.paso)},
            {"texto", paso ? QString::fromStdString(paso->nombre) : QString::fromStdString(p.paso)},
            {"tipo", c != nullptr ? QString::fromStdString(evento::nombreDeTipo(c->tipo)) : QString("paso")},
            {"entro", QString::fromUtf8(evento::linea::nombreDe(p.comoEntro))},
            {"previstoSeg", previsto},
            {"realSeg", real},
            {"desvioSeg", real - previsto}});
        bPrevisto += previsto;
        bReal += real;
        totalPrevisto += previsto;
        totalReal += real;
    }
    cerrarBloque();

    return QVariantMap{{"evento", QString::fromStdString(ab_.evento().nombre)},
                       {"ensayo", corrida_.ensayo()},
                       {"previstoSeg", totalPrevisto},
                       {"realSeg", totalReal},
                       {"desvioSeg", totalReal - totalPrevisto},
                       {"cues", nro},
                       {"bloques", bloques}};
}

QString InformeEventoViewModel::guardarInforme() {
    const QVariantMap inf = informe();
    if (inf.isEmpty() || !ab_.hay()) {
        emit notify(QCoreApplication::translate("EventoViewModel", "Todavía no hay nada que informar: el guion no se corrió"), "error");
        return {};
    }
    const QDir carpeta(QString::fromStdString(ab_.dir().string()));
    const QString nombre = "informe-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".json";
    const QString ruta = carpeta.filePath(nombre);
    // El escritor único, por su cola (fuera de la interfaz; si el disco falla, se reintenta
    // y va al registro).
    ba::persist::cola().escribir(ruta.toStdWString(), QJsonDocument(QJsonObject::fromVariantMap(inf)).toJson().toStdString());
    emit notify(QCoreApplication::translate("EventoViewModel", "Informe guardado en %1").arg(nombre), "ok");
    return ruta;
}

std::string InformeEventoViewModel::lineaExportar(const std::string& formato) const {
    if (formato == "mos") return evento::linea::escaletaMos(ab_.linea(), ab_.carpeta().toStdString(), ab_.evento().nombre);
    const auto hora = [this](int64_t ms) { return corrida_.horaDe(ms).substr(0, 5); };
    const evento::linea::Resolucion r = corrida_.corrida() != nullptr ? corrida_.corrida()->resolver(ahoraMs())
                                                 : evento::linea::Corrida(ab_.linea(), corrida_.entradaBase(false)).resolver(ahoraMs());
    return evento::linea::escaletaCsv(ab_.linea(), &r, corrida_.salidaPorOmision(), hora);
}

json InformeEventoViewModel::lineaImportar(const std::string& formato, const std::string& texto, bool probar, const std::string& nombreBloque) {
    const bool mos = formato == "mos" || (formato.empty() && texto.find("<mos") != std::string::npos);
    const evento::linea::Leida leida = mos ? evento::linea::leerMos(texto) : evento::linea::leerCsv(texto, nombreBloque);
    const json lote = evento::linea::importar(ab_.linea(), leida);
    if (!lote.value("ok", false)) return lote;
    json r = corrida_.lineaEditar(lote["ops"], probar);
    if (!r.value("ok", false)) return r;
    r["ops"] = lote["ops"];
    r["avisos"] = lote["avisos"];
    r["bloques"] = lote["bloques"];
    return r;
}

namespace {
/// Una ruta o la URL que da el diálogo de archivos («file:///…»).
QString rutaLocal(const QString& r) {
    const QUrl u(r);
    return u.isLocalFile() ? u.toLocalFile() : r;
}
} // namespace

QString InformeEventoViewModel::exportarEscaleta(const QString& formato, const QString& rutaOUrl) {
    if (!ab_.hay()) return QCoreApplication::translate("EventoViewModel", "No hay un evento abierto.");
    const QString ruta = rutaLocal(rutaOUrl);
    if (ruta.isEmpty()) return QCoreApplication::translate("EventoViewModel", "No se pudo escribir «%1».").arg(ruta);
    // El escritor único, por su cola (fuera de la interfaz).
    ba::persist::cola().escribir(ruta.toStdWString(), lineaExportar(formato.toStdString()));
    return {};
}

QVariantMap InformeEventoViewModel::importarEscaleta(const QString& rutaOUrl, bool probar) {
    const QString ruta = rutaLocal(rutaOUrl);
    QFile f(ruta);
    if (!f.open(QIODevice::ReadOnly)) return {{"ok", false}, {"error", QCoreApplication::translate("EventoViewModel", "No se pudo leer «%1».").arg(ruta)}};
    const std::string formato = ruta.endsWith(".xml", Qt::CaseInsensitive) || ruta.endsWith(".mos", Qt::CaseInsensitive) ? "mos" : "csv";
    const json r = lineaImportar(formato, f.readAll().toStdString(), probar, QFileInfo(ruta).completeBaseName().toStdString());
    return QJsonDocument::fromJson(QByteArray::fromStdString(r.dump(-1, ' ', false, json::error_handler_t::replace))).object().toVariantMap();
}
} // namespace ba::app
