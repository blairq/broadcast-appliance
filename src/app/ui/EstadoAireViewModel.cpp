// src/app/ui/EstadoAireViewModel.cpp — ver el .hpp

#include "ui/EstadoAireViewModel.hpp"

#include "emision/VentanillaMotor.hpp"

#include <QChar>
#include <QDateTime>

#include <algorithm>

namespace ba::app {

EstadoAireViewModel::EstadoAireViewModel(Partes p, QObject* parent) : QObject(parent), p_(std::move(p)) {
    antes_ = e();
    bytesAntes_ = antes_.bytes;
    bytesAntesMs_ = ahora();
    segundo_.setInterval(1000);
    connect(&segundo_, &QTimer::timeout, this, &EstadoAireViewModel::alSegundo);
    estadisticas_.setSingleShot(true);
    connect(&estadisticas_, &QTimer::timeout, this, &EstadoAireViewModel::avisarEstadisticas);
    p_.ventanilla.escuchar(this, [this](unsigned partes) { alCambiar(partes); });
    if (algoSale()) segundo_.start();
}

EstadoAireViewModel::~EstadoAireViewModel() = default;

const FotoMotor::EstadoAire& EstadoAireViewModel::e() const { return p_.ventanilla.foto().estado; }

std::int64_t EstadoAireViewModel::ahora() const { return p_.ahoraMs ? p_.ahoraMs() : QDateTime::currentMSecsSinceEpoch(); }

QString EstadoAireViewModel::hms(std::int64_t desdeMs, std::int64_t ahoraMs) {
    const std::int64_t s = desdeMs > 0 ? std::max<std::int64_t>(0, (ahoraMs - desdeMs) / 1000) : 0;
    return QString("%1:%2:%3")
        .arg(s / 3600, 2, 10, QChar('0'))
        .arg((s / 60) % 60, 2, 10, QChar('0'))
        .arg(s % 60, 2, 10, QChar('0'));
}

QString EstadoAireViewModel::recordTime() const { return hms(recordStartMs(), ahora()); }
QString EstadoAireViewModel::streamTime() const { return hms(e().transmite ? e().transmiteDesdeMs : 0, ahora()); }

bool EstadoAireViewModel::algoSale() const {
    const FotoMotor& f = p_.ventanilla.foto();
    if (f.estado.graba || f.estado.transmite || f.estado.grabacionCaida) return true;
    return std::any_of(f.salidas.begin(), f.salidas.end(), [](const FotoMotor::Salida& s) { return s.grabando || s.transmitiendo; });
}

void EstadoAireViewModel::alCambiar(unsigned partes) {
    if ((partes & (FotoMotor::Estado | FotoMotor::Salidas)) == 0) return;
    // El reloj de los tiempos anda solo mientras algo sale (en alguna salida).
    if (algoSale() != segundo_.isActive()) {
        if (segundo_.isActive()) segundo_.stop();
        else segundo_.start();
    }
    if ((partes & FotoMotor::Estado) == 0) return;
    const FotoMotor::EstadoAire& n = e();
    const FotoMotor::EstadoAire a = antes_;
    antes_ = n;
    // Un corte pedido es un aviso corto; uno que nadie pidió (red, servidor, disco lleno)
    // queda fijo en la barra del operador hasta que lo cierre.
    if (a.graba && !n.graba) {
        if (n.corteGrabacionPedido) {
            emit notify(tr("Grabación detenida"), "info");
        } else if (n.grabacionCaida && p_.critico) {
            // Se cortó y el motor la rearma sola en otra parte (F1-05). La interfaz NUNCA
            // la para por su cuenta: la grabación es del motor, que la vuelve a armar (y un
            // instante sin grabar también pasa al rearmar el motor, p. ej. tras perder la GPU:
            // pararla ahí la mataba). Queda la alarma record_down hasta que vuelva.
            p_.critico(tr("Se cortó la grabación"),
                       tr("La grabación se cortó y vuelve sola en otra parte del archivo. Si no vuelve, revisá el disco."));
        }
    }
    // Una transmisión que se corta sin que nadie la pare la reconecta el motor solo (se
    // avisa abajo, con «reconectando…»). La interfaz no la para: un instante sin
    // transmitir también pasa al rearmar el motor (la GPU perdida), y pararla ahí la mataba.
    if (a.transmite && !n.transmite && n.corteTransmisionPedido) emit notify(tr("Transmisión detenida"), "warn");
    // Avisar UNA vez por problema, no en cada foto.
    if (n.reconectando && !a.reconectando)
        emit notify(tr("Se cortó la transmisión: reconectando… (%1)").arg(QString::fromStdString(n.problema)), "error");
    else if (!n.reconectando && a.reconectando && n.transmite)
        emit notify(tr("Transmisión recuperada"), "ok");
    if (n.transmite && !a.transmite) {
        bytesAntes_ = n.bytes;
        bytesAntesMs_ = ahora();
    }
    if (!n.transmite && kbps_ != 0) {
        kbps_ = 0;
        avisarEstadisticas();
    }
    const auto aire = [](const FotoMotor::EstadoAire& x) {
        return std::tie(x.graba, x.transmite, x.haySalida, x.hayTransmision, x.reconectando, x.problema, x.grabacionCaida,
                        x.grabaDesdeMs, x.transmiteDesdeMs);
    };
    if (aire(n) != aire(a)) {
        emit aireChanged(); // con las marcas de inicio: el reloj de la vista vuelve a 00:00:00 ya
    }
    const auto estadisticas = [](const FotoMotor::EstadoAire& x) {
        return std::tie(x.fps, x.codificador, x.descartados, x.cuadros);
    };
    if (estadisticas(n) != estadisticas(a)) avisarEstadisticas();
}

void EstadoAireViewModel::alSegundo() {
    // El bitrate real de la transmisión (promedio de ~1 s), de los bytes que cuenta el motor.
    const FotoMotor::EstadoAire& n = e();
    const std::int64_t t = ahora();
    if (n.transmite && t > bytesAntesMs_) {
        const double dt = static_cast<double>(t - bytesAntesMs_) / 1000.0;
        const int kbps = n.bytes >= bytesAntes_ ? static_cast<int>(static_cast<double>(n.bytes - bytesAntes_) * 8.0 / 1000.0 / dt) : 0;
        bytesAntes_ = n.bytes;
        bytesAntesMs_ = t;
        if (kbps != kbps_) {
            kbps_ = kbps;
            avisarEstadisticas();
        }
    }
    emit segundo();
}

void EstadoAireViewModel::avisarEstadisticas() {
    // A lo sumo una vez por segundo: el motor mueve los cuadros en cada vuelta de su reloj.
    if (ultimaEstadistica_.isValid() && ultimaEstadistica_.elapsed() < 1000) {
        if (!estadisticas_.isActive()) estadisticas_.start(static_cast<int>(1000 - ultimaEstadistica_.elapsed()));
        return;
    }
    estadisticas_.stop();
    ultimaEstadistica_.start();
    emit estadisticasChanged();
}

} // namespace ba::app

#include "moc_EstadoAireViewModel.cpp"
