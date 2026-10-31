// src/app/ui/EscritorioViewModel.cpp — ver el .hpp

#include "ui/EscritorioViewModel.hpp"

#include "arranque/AppRuntime.hpp"
#include "diag/DiarioDeArranque.hpp"
#include "pantallas/Catalogo.hpp"
#include "pantallas/Monitores.hpp"
#include "pantallas/Ventanas.hpp"
#include "ptz/ControlCamaras.hpp"
#include "ui/DashboardModel.hpp"
#include "ui/FrameService.hpp"
#include "ui/ImagenViewModel.hpp"
#include "ui/MultiviewViewModel.hpp"
#include "ui/PantallasViewModel.hpp"
#include "ui/RielConfig.hpp"
#include "video/VideoGrafos.hpp"

#include <QGuiApplication>
#include <QQmlEngine>
#include <QScreen>
#include <QWindow>

namespace ba::app {

namespace {

/// Sin padre y devuelto a QML: que no se lo quede su recolector. Es del área.
QObject* deCpp(QObject* o) {
    if (o != nullptr) QQmlEngine::setObjectOwnership(o, QQmlEngine::CppOwnership);
    return o;
}

} // namespace

EscritorioViewModel::EscritorioViewModel(Partes p, QObject* parent) : QObject(parent), p_(std::move(p)) {
    AppRuntime& rt = p_.rt;
    // Las multivistas antes que el catálogo (las lista) y las ventanas (las migran).
    multivistas_ = std::make_unique<MultiviewViewModel>(rt.dataRoot() / "Config");
    CatalogoVisible::Fuentes f;
    f.escenas = [&rt]() {
        QStringList out;
        for (const std::string& n : rt.orderedSceneNames()) out << QString::fromStdString(n);
        return out;
    };
    f.multiviews = [this]() { return multivistas_->lista(); };
    f.fondo = [t = p_.tablero]() { return t ? t->background() : QStringList{}; };
    f.fondoDistribucion = [t = p_.tablero]() { return t ? t->backgroundLayout() : QString("grid"); };
    f.camarasPtz = [this]() {
        QStringList out;
        for (const QVariant& v : p_.imagen.camaras().camaras()) out << v.toMap().value("entrada").toString();
        return out;
    };
    f.paginas = []() { return RielConfig::todasLasPaginas(); };
    f.contenidosRed = [pa = p_.pantallas]() { return pa ? pa->contenidos() : QVariantList{}; };
    // Los tótems de registro: cada uno a una TV, que muestra el QR para registrarse.
    f.totems = [this]() { return p_.totems ? p_.totems() : QVariantList{}; };
    // Las fuentes de video sueltas (invitados, sus pantallas, las entradas):
    // lo que antes solo se podía sacar a una ventana de video por su clave.
    f.videos = [this]() {
        QVariantList out;
        FrameService& cuadros = p_.cuadros;
        for (const QString& k : cuadros.keys()) out << QVariantMap{{"clave", k}, {"titulo", cuadros.label(k)}};
        // Las cámaras de los tótems de registro: para ver en otro monitor quién se
        // está registrando (negro mientras ese canal no conversa con nadie).
        for (int k = 1; k <= FrameService::kRegistroSlots; ++k) {
            const QString clave = QString("registro:%1").arg(k);
            out << QVariantMap{{"clave", clave}, {"titulo", cuadros.label(clave)}};
        }
        for (const QString& e : p_.imagen.video().entradas()) out << QVariantMap{{"clave", "entrada:" + e}, {"titulo", e}};
        return out;
    };
    catalogo_ = std::make_unique<CatalogoVisible>(std::move(f));
    const auto refrescar = [this]() { catalogo_->refrescar(); };
    connect(multivistas_.get(), &MultiviewViewModel::cambio, catalogo_.get(), refrescar);
    if (p_.tablero) connect(p_.tablero, &DashboardModel::backgroundChanged, catalogo_.get(), refrescar);
    connect(&p_.imagen.camaras(), &ptz::ControlCamaras::cambio, catalogo_.get(), refrescar);
    connect(&p_.cuadros, &FrameService::keysChanged, catalogo_.get(), refrescar);
    monitores_ = std::make_unique<MonitoresModel>(rt.dataRoot() / "Config" / "monitores.json", catalogo_.get());
    monitores_->conectarAlSistema();
    // Las ventanas sueltas (pantallas/Ventanas.hpp): las mismas cosas del
    // catálogo y los mismos monitores. La primera vez trae las ventanas de
    // video que había en ui.json.
    {
        ba::diag::Cargando c("ventanas");
        ventanas_ = std::make_unique<VentanasModel>(c.saltear() ? std::filesystem::path{} : rt.dataRoot() / "Config" / "ventanas.json",
                                                    catalogo_.get(), monitores_.get());
        if (!c.saltear()) ventanas_->migrarDesde(rt.dataRoot() / "Config" / "ui.json", multivistas_->actual());
    }
    multivistas_->setVentanas(ventanas_.get(), monitores_.get());
    connect(monitores_.get(), &MonitoresModel::cambio, this, &EscritorioViewModel::monitoresCambio);
    // Las entradas de video también son cosas que se pueden mostrar (catálogo).
    connect(&p_.imagen.video(), &video::VideoGrafos::cambio, catalogo_.get(), refrescar);
}

EscritorioViewModel::~EscritorioViewModel() = default;

QObject* EscritorioViewModel::catalogoQml() const { return deCpp(catalogo_.get()); }
QObject* EscritorioViewModel::monitoresQml() const { return deCpp(monitores_.get()); }
QObject* EscritorioViewModel::ventanasQml() const { return deCpp(ventanas_.get()); }
QObject* EscritorioViewModel::multivistasQml() const { return deCpp(multivistas_.get()); }

void EscritorioViewModel::refrescarCatalogo() { catalogo_->refrescar(); }

QVariantList EscritorioViewModel::monitores() const {
    // Los de Salidas → Pantallas: orden físico e ids estables (una sola lista).
    return monitores_->conectadosEnOrden();
}

bool EscritorioViewModel::puedeUbicarVentanas() { return !QGuiApplication::platformName().contains("wayland", Qt::CaseInsensitive); }

QString EscritorioViewModel::identificarPantallas(const QString& cual) {
    if (cual.trimmed().isEmpty()) {
        emit identificarPedido({});
        return {};
    }
    const QString id = monitores_->resolver(cual);
    if (id.isEmpty()) return tr("No hay un monitor «%1»: decí su número, su nombre o su id.").arg(cual);
    const QVariantMap m = monitores_->monitor(id);
    if (!m.value("conectado").toBool()) return tr("El monitor «%1» no está conectado.").arg(cual);
    emit identificarPedido({id});
    return {};
}

QStringList EscritorioViewModel::ventanasAbiertas() const {
    QStringList out;
    for (QWindow* w : QGuiApplication::topLevelWindows()) {
        if (w == nullptr || !w->isVisible() || !w->objectName().startsWith("ba:")) continue;
        if (w->screen() != nullptr && !out.contains(w->screen()->name())) out << w->screen()->name();
    }
    return out;
}

} // namespace ba::app
