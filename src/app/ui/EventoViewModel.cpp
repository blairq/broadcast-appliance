// ============================================================================
// src/app/ui/EventoViewModel.cpp — ver EventoViewModel.hpp
// ============================================================================
#include "ui/EventoViewModel.hpp"

#include <QPointer>
#include <QTimer>

#include "ajustes/Ajustes.hpp"
#include "arranque/AppRuntime.hpp"
#include "evento/EventoWeb.hpp"
#include "i18n/I18n.hpp"
#include "platform/Paths.hpp"
#include "ui/CarruselViewModel.hpp"

namespace ba::app {

EventoViewModel::EventoViewModel(AppRuntime& rt, VentanillaMotor& ventanilla, ConferenceViewModel* sala, graficos::Emisor* graficos,
                                 bool sinAbrirUltimo, QObject* parent)
    : QObject(parent), rt_(rt), graficos_(graficos) {
    ab_ = new EventoAbierto(rt.dataRoot() / "Eventos", this);
    lista_ = new EventosListaViewModel(rt.dataRoot() / "Eventos", rt.dataRoot() / "Config" / "historico.json", this);
    guion_ = new GuionViewModel(*ab_, rt, ventanilla, sala, graficos, this);
    padron_ = new PadronViewModel(*ab_, sala, this);
    corrida_ = new CorridaViewModel(*ab_, *guion_, rt, ventanilla, sala, graficos, this);
    web_ = new WebEventoViewModel(*ab_, *corrida_, *padron_, *lista_, rt, sala, this);
    informe_ = new InformeEventoViewModel(*ab_, *corrida_, *padron_, this);

    // Los avisos de todas las piezas, juntos.
    for (QObject* o : std::initializer_list<QObject*>{lista_, guion_, padron_, corrida_, web_, informe_})
        connect(o, SIGNAL(notify(QString, QString)), this, SIGNAL(notify(QString, QString)));

    // Guardar el abierto: la lista lo ve ya y su página web toma las secciones nuevas.
    connect(guion_, &GuionViewModel::guardado, this, [this]() {
        lista_->alGuardar(ab_->carpeta().toStdString(), ab_->evento());
        web_->asegurarBase();
    });
    // Correr de verdad lo pone en curso: lo que pase desde ahora es de este evento.
    corrida_->setPonerEnCurso([this]() {
        if (lista_->carpetaEnCurso() != ab_->carpeta().toStdString()) iniciarEvento(ab_->carpeta());
    });
    corrida_->setAbrir([this](const QString& c) { return abrir(c); });

    // Las plantillas de la página web del evento (Web/evento/plantilla del programa): ANTES
    // de abrir el último evento, que al abrirse actualiza sus páginas con ellas.
    evento::setCarpetaPlantillas(ba::platform::bundleRoot() / "Web" / "evento" / "plantilla");
    // Se abre solo el evento que se guardó último: el operador trabaja en el mismo durante
    // días, y abrir el programa para encontrarlo vacío es una molestia diaria. La fecha
    // del archivo ya lo dice.
    const std::string ultimo = sinAbrirUltimo ? std::string() : ab_->store().ultimoTocado();
    if (!ultimo.empty()) abrir(QString::fromStdString(ultimo));
    // EL EVENTO EN CURSO: el que quedó anotado sigue (un corte de luz no lo corta); si no
    // hay ninguno y está prendido el automático, el del día. Cada minuto se mira de nuevo:
    // a la medianoche el automático pasa al de hoy (sin ir al disco si no hace falta).
    lista_->asegurarEnCurso();
    // Tras un corte de luz, la línea que estaba corriendo vuelve en HOLD y pregunta qué
    // está pasando (docs/planes/PLAN-LINEA-DE-TIEMPO.md 3.8). Un rato después, como la
    // playlist: que el aire ya haya vuelto (T47) y estén el conmutador y los gráficos.
    // Sin corte (se cerró bien), lo que haya quedado escrito no es de un corte.
    if (rt.corteAnterior())
        QTimer::singleShot(ba::ajustes::entero("arranque.retomarLineaMs", 2500), corrida_, &CorridaViewModel::retomarLinea);
    else
        corrida_->borrarVivo();
    vigiaEnCurso_.setInterval(ba::ajustes::sAMs("evento.vigiaS", 60));
    connect(&vigiaEnCurso_, &QTimer::timeout, lista_, &EventosListaViewModel::asegurarEnCurso);
    vigiaEnCurso_.start();
}

EventoViewModel::~EventoViewModel() {
    // Al revés de como se armaron: cada pieza vive menos que las que lee.
    delete informe_;
    delete web_;
    delete corrida_;
    delete padron_;
    delete guion_;
    delete lista_;
    delete ab_;
}

void EventoViewModel::setCarruseles(CarruselViewModel* c) {
    guion_->setCarruseles(c);
    corrida_->setCarruseles(c);
}

// --- Abrir, cerrar, crear, borrar -------------------------------------------------------------

bool EventoViewModel::abrir(const QString& carpeta) {
    // No se pierde trabajo por abrir otra cosa: se guarda lo que había.
    if (ab_->sucio()) guion_->guardar();
    bool ok = false;
    std::string err;
    evento::Evento e = ab_->leer(carpeta.toStdString(), &ok, &err);
    if (!ok) {
        emit notify(tr("No se pudo abrir el evento: %1").arg(QString::fromStdString(err)), "error");
        return false;
    }
    corrida_->olvidar();
    ba::i18n::usarEnEvento(e.idioma); // lo que ven y escuchan el público y los invitados
    const QString aviso = ab_->poner(carpeta, std::move(e));
    corrida_->limpiarBitacora();
    if (!aviso.isEmpty()) corrida_->anotar(aviso, "info");
    padron_->cargar();
    // La lista blanca vieja del lobby pasa al padrón una sola vez: el guion se guarda sin
    // ella para que la migración no se repita en cada apertura.
    if (padron_->migrarAutoAceptar(ab_->evento().autoAceptar)) {
        ab_->editar().autoAceptar.clear();
        ab_->guardar();
    }
    // Su página web: las páginas que nadie editó toman la plantilla de este programa y se
    // crean las que falten. Lo editado no se toca.
    web_->asegurarBase();
    // Lo que la línea mira afuera; un bloque del orden del día se arma ya.
    corrida_->enchufar();
    return true;
}

void EventoViewModel::cerrar() {
    if (ab_->sucio()) guion_->guardar();
    corrida_->olvidar();
    ab_->cerrar();
    ba::i18n::usarEnEvento({});
    padron_->cargar();
}

void EventoViewModel::borrar(const QString& carpeta) {
    // Si es el abierto, se cierra ANTES de borrarlo y sin guardar: cerrar guarda lo
    // pendiente, y hacerlo después volvía a escribir en el disco el evento recién borrado.
    if (ab_->carpeta() == carpeta) {
        corrida_->olvidar();
        ab_->cerrar();
        ba::i18n::usarEnEvento({});
        padron_->cargar();
    }
    QPointer<EventoViewModel> yo(this);
    lista_->borrar(carpeta.toStdString(), [yo](const std::string& err) {
        if (yo && !err.empty()) emit yo->notify(tr("No se pudo borrar: %1").arg(QString::fromStdString(err)), "error");
    });
}

QString EventoViewModel::crear(const QString& nombre, const QString& enCarpeta) {
    std::string err;
    const std::string carpeta = lista_->crear(nombre.toStdString(), enCarpeta.toStdString(), &err);
    if (carpeta.empty()) {
        emit notify(tr("No se pudo crear el evento: %1").arg(QString::fromStdString(err)), "error");
        return {};
    }
    // Todo evento nuevo nace con su página web básica (la plantilla): abrir la arma.
    abrir(QString::fromStdString(carpeta));
    return QString::fromStdString(carpeta);
}

bool EventoViewModel::iniciarEvento(const QString& carpeta) {
    if (lista_->carpetaEnCurso() == carpeta.toStdString()) return true;
    std::string err;
    if (!lista_->iniciar(carpeta.toStdString(), &err)) {
        emit notify(tr("No se pudo poner en curso: %1").arg(QString::fromStdString(err)), "error");
        return false;
    }
    // Su página web con dirección propia: que la dirección pública pase a ser esa.
    if (const std::string sub = web_->subdominioAlIniciar(carpeta.toStdString()); !sub.empty())
        emit pedirDireccion(QString::fromStdString(sub));
    return true;
}

QString EventoViewModel::moverEvento(const QString& carpeta, const QString& aCarpeta) {
    if (carpeta == ab_->carpeta() && ab_->sucio()) guion_->guardar();
    std::string err;
    const std::string nueva = lista_->mover(carpeta.toStdString(), aCarpeta.toStdString(), &err);
    if (nueva.empty()) {
        emit notify(tr("No se pudo mover: %1").arg(QString::fromStdString(err)), "error");
        return {};
    }
    if (ab_->carpeta() == carpeta) ab_->renombrar(QString::fromStdString(nueva));
    return QString::fromStdString(nueva);
}

std::filesystem::path EventoViewModel::carpetaEvento(const std::string& carpeta) const {
    // Sin carpeta: el en curso, de la caché (sin ir al disco a preguntar cuál es).
    const std::string c = carpeta.empty() ? lista_->carpetaEnCurso() : carpeta;
    return c.empty() ? std::filesystem::path() : ab_->store().carpetaExistente(c);
}

std::filesystem::path EventoViewModel::archivoBitacoraDe(const std::string& carpeta) const {
    const auto d = carpetaEvento(carpeta);
    return d.empty() ? d : d / "bitacora.jsonl";
}

std::filesystem::path EventoViewModel::carpetaConfig() const { return rt_.dataRoot() / "Config"; }

} // namespace ba::app
