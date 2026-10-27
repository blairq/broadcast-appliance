// ============================================================================
// src/app/pantallas/Catalogo.cpp — ver Catalogo.hpp
// ============================================================================
#include "pantallas/Catalogo.hpp"

#include "pantallas/Tarjetas.hpp"

#include <QCoreApplication>
#include <QRegularExpression>

#include <set>

namespace ba::app {
namespace {

/// Los paneles sueltos que no son una tarjeta del tablero (o que son MÁS que
/// su tarjeta: el Mezclador y el Carrusel tienen panel propio). `tarjeta` es
/// el tipo de tarjeta que, sacada a ventana, abre este panel.
struct Panel {
    const char* id;
    const char* titulo;
    const char* icono;
    const char* tarjeta;
    const char* redTipo; ///< el contenido de la TV que muestra lo mismo, o ""
};
// El título se traduce al armar la lista: QCoreApplication::translate("pantallas", titulo).
const Panel kPaneles[] = {
    {"padron", QT_TRANSLATE_NOOP("pantallas", "Padrón"), "☰", "", ""},
    {"preguntas", QT_TRANSLATE_NOOP("pantallas", "Preguntas del público"), "?", "", ""},
    {"votacion", QT_TRANSLATE_NOOP("pantallas", "Votación"), "☑", "", "votacion"},
    {"carrusel", QT_TRANSLATE_NOOP("pantallas", "Carrusel"), "⟳", "carrusel", ""},
    {"mezclador", QT_TRANSLATE_NOOP("pantallas", "Mezclador"), "🎚", "mixer", ""},
};

/// Las tarjetas que tienen panel propio no se repiten como «tarjeta:<kind>».
bool tienePanelPropio(const QString& kind) {
    if (kind == "ptz") return true; // uno por cámara: «ptz:<entrada>»
    for (const Panel& p : kPaneles)
        if (kind == QString::fromUtf8(p.tarjeta)) return true;
    return false;
}

/// Las páginas cuyo contenido también existe en la TV.
QString redDePagina(const QString& idPagina) { return idPagina == "prompter" ? QStringLiteral("prompter") : QString(); }

} // namespace

QString CatalogoVisible::tituloDeGrupo(const QString& g) {
    if (g == "programa") return QCoreApplication::translate("pantallas", "Salida");
    if (g == "escena") return QCoreApplication::translate("pantallas", "Escena");
    if (g == "multiview") return QStringLiteral("Multiview");
    if (g == "video") return QStringLiteral("Video");
    if (g == "tarjeta") return QCoreApplication::translate("pantallas", "Tarjeta del tablero");
    if (g == "panel") return QStringLiteral("Panel");
    if (g == "pagina") return QCoreApplication::translate("pantallas", "Página");
    if (g == "fondo") return QCoreApplication::translate("pantallas", "Tablero");
    if (g == "web") return QCoreApplication::translate("pantallas", "Para la TV");
    if (g == "registro") return QCoreApplication::translate("pantallas", "Registro de personas");
    return g;
}

CatalogoVisible::CatalogoVisible(Fuentes fuentes, QObject* parent)
    : ListaConClave(parent), fuentes_(std::move(fuentes)) {
    // Primero las filas: los filtros de abajo las leen al engancharse.
    filas_ = armar();
    // Dos vistas filtradas del MISMO modelo (no dos listas): los selectores de
    // los monitores y los de las TV.
    const auto filtro = [this](int rol) {
        auto* p = new QSortFilterProxyModel(this);
        p->setSourceModel(this);
        p->setFilterRole(rol);
        p->setFilterRegularExpression(QRegularExpression(QStringLiteral("^true$")));
        return p;
    };
    locales_ = filtro(LocalRole);
    deRed_ = filtro(RedRole);
}

QVariant CatalogoVisible::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount()) return {};
    const Cosa& c = filas_[static_cast<std::size_t>(index.row())];
    switch (role) {
        case IdRole: return c.id;
        case GrupoRole: return c.grupo;
        case GrupoTituloRole: return tituloDeGrupo(c.grupo);
        case TituloRole: return c.titulo;
        case EtiquetaRole:
            // Para un selector plano: «Escena — Plano general». El programa y
            // el fondo ya dicen qué son.
            return c.grupo == "programa" || c.grupo == "fondo" ? c.titulo : tituloDeGrupo(c.grupo) + " — " + c.titulo;
        case IconoRole: return c.icono;
        case DibujoRole: return c.dibujo;
        case LocalRole: return !c.dibujo.isEmpty();
        case RedRole: return !c.redTipo.isEmpty();
        case RedTipoRole: return c.redTipo;
        case RedDatosRole: return c.redDatos;
        case TarjetaRole: return c.tarjeta;
        default: return {};
    }
}

QHash<int, QByteArray> CatalogoVisible::roleNames() const {
    return {{IdRole, "itemId"},       {GrupoRole, "grupo"},       {GrupoTituloRole, "grupoTitulo"},
            {TituloRole, "titulo"},   {EtiquetaRole, "etiqueta"}, {IconoRole, "icono"},
            {DibujoRole, "dibujo"},   {LocalRole, "local"},       {RedRole, "red"},
            {RedTipoRole, "redTipo"}, {RedDatosRole, "redDatos"}, {TarjetaRole, "tarjeta"}};
}

std::vector<CatalogoVisible::Cosa> CatalogoVisible::armar() const {
    std::vector<Cosa> out;
    const auto video = [](const QStringList& claves, const QString& distribucion = QStringLiteral("grid")) {
        return QVariantMap{{"modo", "video"}, {"claves", claves}, {"distribucion", distribucion}};
    };

    // 1) Las salidas: lo que se mira todo el tiempo, arriba.
    out.push_back({"programa", "programa", QCoreApplication::translate("pantallas", "Programa (lo que sale al aire)"), "●",
                   video({"program"}), "programa", {}, ""});
    out.push_back({"preview", "programa", QCoreApplication::translate("pantallas", "Preview (la escena armada)"), "◐",
                   video({"armada"}), "", {}, ""});

    // 2) Lo que va de fondo en el tablero: SIGUE al tablero (si el operador
    //    cambia el fondo, el monitor que lo muestra cambia con él).
    {
        const QStringList claves = fuentes_.fondo ? fuentes_.fondo() : QStringList{};
        const QString dist = fuentes_.fondoDistribucion ? fuentes_.fondoDistribucion() : QStringLiteral("grid");
        Cosa c{"fondo", "fondo", QCoreApplication::translate("pantallas", "Lo de fondo en el tablero"), "▣", {}, "", {}, ""};
        // El fondo puede ser una multivista («multiview:<id>» en la distribución).
        if (dist.startsWith("multiview:"))
            c.dibujo = QVariantMap{{"modo", "multiview"}, {"id", dist.mid(10)}};
        else if (!claves.isEmpty())
            c.dibujo = video(claves, dist);
        out.push_back(c);
    }

    // 3) Las escenas: cada una en vivo, esté o no al aire.
    if (fuentes_.escenas)
        for (const QString& e : fuentes_.escenas()) {
            if (e.isEmpty()) continue;
            Cosa c{"escena:" + e, "escena", e, "▦", video({"escena:" + e}), "escena", QVariantMap{{"escena", e}}, ""};
            c.dibujo["escena"] = e;
            out.push_back(c);
        }

    // 4) Las multivistas.
    if (fuentes_.multiviews)
        for (const QVariant& v : fuentes_.multiviews()) {
            const QVariantMap m = v.toMap();
            const QString id = m.value("id").toString();
            if (id.isEmpty()) continue;
            const QString nombre = m.value("nombre").toString();
            out.push_back({"multiview:" + id, "multiview", nombre.isEmpty() ? id : nombre, "▥",
                           QVariantMap{{"modo", "multiview"}, {"id", id}}, "", {}, ""});
        }

    // 4b) Las fuentes de video sueltas: un invitado, una cámara, a pantalla
    //     entera. Antes solo se podían sacar a una ventana de video aparte
    //     (con su lista propia de claves); ahora son cosas del catálogo como
    //     cualquier otra, y van también a un monitor.
    if (fuentes_.videos) {
        std::set<QString> vistas;
        for (const QVariant& v : fuentes_.videos()) {
            const QVariantMap m = v.toMap();
            const QString clave = m.value("clave").toString();
            // El programa y la armada ya están arriba, con su nombre.
            if (clave.isEmpty() || clave == "program" || clave == "armada" || clave.startsWith("escena:") ||
                !vistas.insert(clave).second)
                continue;
            const QString titulo = m.value("titulo").toString();
            out.push_back({"video:" + clave, "video", titulo.isEmpty() ? clave : titulo, "▶", video({clave}), "", {}, ""});
        }
    }

    // 5) Los paneles (los que son más que su tarjeta, y los del evento).
    for (const Panel& p : kPaneles) {
        const QString id = QString::fromUtf8(p.id);
        out.push_back({id, "panel", QCoreApplication::translate("pantallas", p.titulo), QString::fromUtf8(p.icono),
                       QVariantMap{{"modo", "panel"}, {"panel", id}}, QString::fromUtf8(p.redTipo), {},
                       QString::fromUtf8(p.tarjeta)});
    }
    if (fuentes_.camarasPtz)
        for (const QString& e : fuentes_.camarasPtz()) {
            if (e.isEmpty()) continue;
            out.push_back({"ptz:" + e, "panel", "PTZ — " + e, "⌖", QVariantMap{{"modo", "panel"}, {"panel", "ptz:" + e}},
                           "", {}, "ptz"});
        }

    // 6) El resto de las tarjetas del tablero, cada una en su ventana.
    for (const auto& t : tarjetas::kTipos) {
        const QString kind = QString::fromUtf8(t.id);
        if (tienePanelPropio(kind)) continue;
        out.push_back({"tarjeta:" + kind, "tarjeta", QCoreApplication::translate("tarjetas", t.titulo), QString::fromUtf8(t.icono),
                       QVariantMap{{"modo", "panel"}, {"panel", "tarjeta:" + kind}}, "", {}, kind});
    }

    // 7) Las páginas enteras.
    if (fuentes_.paginas)
        for (const QVariant& v : fuentes_.paginas()) {
            const QVariantMap p = v.toMap();
            const QString id = p.value("id").toString();
            if (id.isEmpty()) continue;
            out.push_back({"pagina:" + id, "pagina", p.value("titulo").toString(), p.value("icon").toString(),
                           QVariantMap{{"modo", "pagina"}, {"pagina", p.value("page").toInt()}, {"idPagina", id}},
                           redDePagina(id), {}, ""});
        }

    // 8) Lo que solo existe en la TV (un cartel, un QR, la hora…): los
    //    contenidos del cast que ninguna cosa de arriba ya representa. Así un
    //    contenido nuevo de la TV aparece solo, sin tocar esta lista.
    if (fuentes_.contenidosRed) {
        std::set<QString> cubiertos;
        for (const Cosa& c : out)
            if (!c.redTipo.isEmpty()) cubiertos.insert(c.redTipo);
        for (const QVariant& v : fuentes_.contenidosRed()) {
            const QVariantMap m = v.toMap();
            const QString tipo = m.value("id").toString();
            if (tipo.isEmpty() || cubiertos.count(tipo) != 0) continue;
            out.push_back({"web:" + tipo, "web", m.value("titulo").toString(), "⧉", {}, tipo, {}, ""});
        }
    }

    // 9) Los tótems de registro: una página web (contenido «url») con su QR.
    //    Van DESPUÉS de lo de arriba a propósito: si contaran como «url» ya
    //    cubierta, «Una página web» suelta desaparecería del catálogo.
    if (fuentes_.totems)
        for (const QVariant& v : fuentes_.totems()) {
            const QVariantMap m = v.toMap();
            const QString id = m.value("id").toString();
            const QString url = m.value("urlPantalla").toString();
            if (id.isEmpty() || url.isEmpty()) continue;
            out.push_back({"registro:" + id, "registro", m.value("nombre").toString(), "☺", {}, "url",
                           QVariantMap{{"url", url}}, ""});
        }
    return out;
}

void CatalogoVisible::refrescar() {
    if (aplicar(armar())) emit cambio();
}

const CatalogoVisible::Cosa* CatalogoVisible::buscar(const QString& id) const {
    for (const Cosa& c : filas_)
        if (c.id == id) return &c;
    return nullptr;
}

namespace {
QVariantMap comoMapa(const QAbstractItemModel& m, int fila) {
    QVariantMap out;
    const auto roles = m.roleNames();
    for (auto it = roles.begin(); it != roles.end(); ++it)
        out.insert(QString::fromUtf8(it.value()), m.data(m.index(fila, 0), it.key()));
    return out;
}
} // namespace

QVariantMap CatalogoVisible::item(const QString& id) const {
    const Cosa* c = buscar(id);
    return c == nullptr ? QVariantMap{} : comoMapa(*this, static_cast<int>(c - filas_.data()));
}

QVariantMap CatalogoVisible::porId() const {
    QVariantMap out;
    for (int i = 0; i < rowCount(); ++i) out.insert(filas_[static_cast<std::size_t>(i)].id, comoMapa(*this, i));
    return out;
}

QString CatalogoVisible::tituloDe(const QString& id) const {
    if (const Cosa* c = buscar(id)) return data(index(static_cast<int>(c - filas_.data())), EtiquetaRole).toString();
    // Lo que se recordaba y ya no está (una escena borrada, una cámara que se
    // fue): se dice qué era, para que el operador entienda el hueco.
    const int dos = id.indexOf(':');
    return QCoreApplication::translate("pantallas", "%1 (ya no existe)").arg(dos > 0 ? id.mid(dos + 1) : id);
}

QString CatalogoVisible::ventanaDeTarjeta(const QString& kind) const {
    for (const Cosa& c : filas_)
        if (c.tarjeta == kind && !c.dibujo.isEmpty()) return c.id;
    return {};
}

} // namespace ba::app
