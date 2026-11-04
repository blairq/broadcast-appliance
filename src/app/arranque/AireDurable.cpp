// ============================================================================
// src/app/arranque/AireDurable.cpp — ver AireDurable.hpp
// ============================================================================

#include "arranque/AireDurable.hpp"

#include <algorithm>
#include <chrono>

#include <nlohmann/json.hpp>

#include "GpuPerdida.hpp" // motor: la GPU perdida (F3-09)
#include "arranque/RuntimeMotor.hpp"
#include "arranque/RepositorioEscenas.hpp"
#include "arranque/ServicioEmision.hpp"
#include "diag/Registro.hpp"
#include "emision/SalidasConfig.hpp"
#include "escenas/RefEscena.hpp"
#include "i18n/I18nTexto.hpp"
#include "persist/Escritor.hpp"
#include "platform/Paths.hpp"

namespace ba::app {

using Cambio = AppRuntime::Cambio;

namespace {
/// El lienzo para componer en la CPU (F3-09): a 720p como mucho, con la misma forma. No
/// se guarda: el próximo arranque vuelve a probar la GPU con el de la configuración.
VideoConfig videoParaCpu(VideoConfig v) {
    constexpr int kAlto = 720;
    const auto achicar = [](int& w, int& h) {
        if (h <= kAlto || h <= 0) return;
        w = (w * kAlto / h) & ~1;
        h = kAlto;
    };
    achicar(v.baseWidth, v.baseHeight);
    achicar(v.outputWidth, v.outputHeight);
    return v;
}
} // namespace

void AireDurable::listo() {
    aireListo_ = true;
    writeOnAirState();
}

void AireDurable::cerrar() {
    // Cierre ordenado: nada que retomar en el proximo arranque (en ninguna salida). Salvo
    // en un corte de luz con poca batería (F1-11): el que cierra es el sistema que se apaga,
    // no el operador; al-aire.json queda como estaba (sin «cerrado») y al volver se retoma.
    if (!emergenciaEnergia_) {
        rt_.emision_->pedidos().olvidarTodo();
        cerrando_ = true;
    }
    writeOnAirState();
    documento_.volcar(); // el dueño (el plano de control) puede estar ya parado
}

std::vector<std::string> AireDurable::cerrarGrabacionesPorEnergia() {
    emergenciaEnergia_ = true;
    std::vector<std::string> cerradas;
    for (const std::string& id : rt_.motorPropio().salidaIds()) {
        if (rt_.emision_->pedidos().de(id).graba == salidas::Pedidos::Estado::No) continue;
        // Como la para el operador (EOS: el archivo queda cerrado y se abre), pero sigue
        // pedida: se marca ANTES de pararla, así el documento de aire nunca dice «no graba»
        // (justo antes de que se apague el equipo es cuando más importa).
        rt_.emision_->pedidos().cerrarPorEnergia(id);
        rt_.motorPropio().stopRecording(id);
        cerradas.push_back(id);
    }
    if (!cerradas.empty()) {
        writeOnAirState();
        rt_.avisar(Cambio::Salidas);
    }
    return cerradas;
}

void AireDurable::retomarGrabacionesPorEnergia() {
    emergenciaEnergia_ = false;
    for (const std::string& id : rt_.emision_->pedidos().tomarCerradasPorEnergia()) rt_.startRecording(id == salidas::kPorOmision ? std::string() : id);
    writeOnAirState();
}

salidas::AlAire AireDurable::aireActual() const {
    const TurnoMotor::Guarda turno(rt_.motorPropio().turno());
    // Cada salida con su escena, lo que transmite (la principal y sus destinos) y si
    // graba; la por omisión además en los campos de siempre (salidas::aJson), que es
    // lo que lee un arranque de antes de las salidas.
    salidas::AlAire a;
    a.cerrado = cerrando_;
    for (const std::string& id : rt_.motorPropio().salidaIds()) {
        const bool po = id == salidas::kPorOmision;
        salidas::Aire& x = a.salidas[id];
        x.escena = rt_.motorPropio().currentScene(id);
        x.escenaId = rt_.motorPropio().currentSceneId(id);
        // LO PEDIDO (pedidos_), no lo que sale: lo que cerró el corte de luz sigue pedido y
        // vuelve con la energía; lo de un corte que espera la carpeta del evento
        // (retomarGrabaciones), también. Una orden que falló ya no está pedida.
        const salidas::Pedidos::DeSalida p = rt_.emision_->pedidos().de(id);
        x.transmitiendo = p.transmitePedida();
        x.grabando = p.grabaPedida() || (grabacionesPorRetomar_ && aireAnterior_.de(id).grabando);
        x.destinos.assign(p.destinos.begin(), p.destinos.end());
        x.placa = salidas::placaDeJson(rt_.motorPropio().estadoPlaca(po ? std::string() : id));
    }
    return a;
}

void AireDurable::writeOnAirState() {
    const TurnoMotor::Guarda turno(rt_.motorPropio().turno());
    // Mientras vuelve como estaba, el documento sigue diciendo lo de la corrida anterior:
    // un corte en pleno arranque vuelve a eso, no a un arranque a medias.
    if (!aireListo_) return;
    // La sección de las salidas del documento único (F3-04). El documento compara: si no
    // cambió nada no despierta a nadie; si cambió, lo escribe su dueño entero, en dos
    // generaciones, primero que los ajustes.
    documento_.ponerSalidas(aireActual().salidas, cerrando_);
}

void AireDurable::aireCambio() {
    writeOnAirState();
    rt_.fotoSucia(FotoMotor::Salidas); // el negro y la placa de cada salida, en la foto
}

void AireDurable::leerAireAnterior() {
    documento_.setCarpeta(ba::platform::stateDir());
    const salidas::DocumentoAire::Anterior l = documento_.leerAnterior();
    aireAnterior_ = l.aire;
    lineaDesconocida_ = l.danado || l.lineaDanada;
    if (l.descartadas > 0 && !l.danado)
        ba::diag::registro().linea(ba::diag::Nivel::Aviso, "arranque", 0,
                                   ba::i18n::traducir("arranque", "al-aire.json: la última generación estaba rota (corte a "
                                                                  "mitad); se retoma la anterior"));
    if (l.deVersionVieja)
        ba::diag::registro().linea(ba::diag::Nivel::Info, "arranque", 0,
                                   ba::i18n::traducir("arranque", "al-aire.json de la versión anterior: placa.json y "
                                                                  "linea-aire.json pasan a ser secciones del documento de aire"));
    if (l.danado) {
        // Las dos generaciones rotas (o el archivo viejo roto): no se sabe qué había al
        // aire. Lo peor es arrancar callado con «nada al aire»: se trata como un corte
        // (la línea de tiempo vuelve en HOLD y pregunta qué está pasando) y se avisa.
        avisarPersistencia(ba::i18n::traducir("arranque", "El estado de aire guardado (al-aire.json) volvió dañado: no se "
                                                          "sabe qué había al aire. Revisá cada salida (escena, transmisión, "
                                                          "grabación) y ponela como corresponde."));
    } else if (l.lineaDanada) {
        avisarPersistencia(ba::i18n::traducir("arranque", "El estado de la línea de tiempo volvió dañado tras el corte: la "
                                                          "línea quedó en HOLD sin saber en qué paso iba. Contá qué está "
                                                          "pasando y reenganchala a mano."));
    }
    corteAnterior_ = !aireAnterior_.cerrado;
    // Un sistema de archivos sin diario (FAT, exFAT, típico de un disco USB) puede
    // romper la carpeta entera en un corte, aunque cada archivo se escriba bien.
    if (ba::persist::sinDiario(rt_.root_))
        avisarPersistencia(ba::i18n::llenar(ba::i18n::traducir("arranque", "La carpeta de datos está en un disco %1 (sin "
                                                                            "diario): un corte de luz puede dañarla. Pasala "
                                                                            "a un disco APFS, NTFS o ext4."),
                                            {ba::persist::sistemaDeArchivos(rt_.root_)}));
}

std::vector<std::string> AireDurable::avisosPersistencia() const {
    std::lock_guard<std::mutex> l(avisosMu_);
    return avisosPersistencia_;
}

void AireDurable::avisarPersistencia(const std::string& texto) {
    ba::diag::registro().linea(ba::diag::Nivel::Error, "persistencia", 0, texto);
    std::lock_guard<std::mutex> l(avisosMu_);
    if (std::find(avisosPersistencia_.begin(), avisosPersistencia_.end(), texto) == avisosPersistencia_.end())
        avisosPersistencia_.push_back(texto);
}

void AireDurable::retomarGrabaciones() {
    if (!grabacionesPorRetomar_) return;
    grabacionesPorRetomar_ = false;
    for (const auto& [id, s] : aireAnterior_.salidas) {
        if (!s.grabando || !rt_.motorPropio().existeSalida(id)) continue;
        if (rt_.emision_->pedidos().de(id).grabaPedida()) continue;
        ba::diag::registro().linea(ba::diag::Nivel::Aviso, "arranque", 0,
                                   ba::i18n::llenar(ba::i18n::traducir("arranque", "la corrida anterior no se cerró (corte de luz o cuelgue): se retoma la grabación de la salida «%1»"), {id}));
        if (!rt_.startRecording(id))
            ba::diag::registro().linea(ba::diag::Nivel::Error, "arranque", 0,
                                       ba::i18n::llenar(ba::i18n::traducir("arranque", "la grabación de la salida «%1» no se pudo retomar: %2"), {id, AppRuntime::errorDelHilo()}));
    }
}

void AireDurable::restaurarAire() {
    const TurnoMotor::Guarda turno(rt_.motorPropio().turno());
    const salidas::AlAire& aire = aireAnterior_;
    // La escena que estaba al aire (app.json puede tener una vieja: no se guarda en cada
    // corte). También con --test: no sale nada al aire.
    // Por el id (un renombre después de escrito no la pierde); uno de antes, por el nombre.
    const escenas::TablaEscenas t = rt_.motorPropio().tablaEscenas();
    const salidas::Aire& po = aire.de(salidas::kPorOmision);
    const std::string escena = escenas::apunta(po.escenaId, po.escena, t);
    if (corteAnterior_ && !escena.empty() && rt_.motorPropio().sceneId(escena) != rt_.motorPropio().currentSceneId() && rt_.setScene(escena))
        ba::diag::registro().linea(ba::diag::Nivel::Aviso, "arranque", 0,
                                   ba::i18n::llenar(ba::i18n::traducir("arranque", "la corrida anterior no se cerró: vuelve la escena «%1»"),
                                                    {rt_.motorPropio().sceneName(escena)}));
    // Las demás salidas: su escena solo está ahí (no van en app.json), así que vuelve siempre.
    for (const std::string& id : rt_.motorPropio().salidaIds()) {
        const std::string e = escenas::apunta(aire.de(id).escenaId, aire.de(id).escena, t);
        if (id != salidas::kPorOmision && !e.empty() && !rt_.setScene(e, id))
            ba::diag::registro().linea(ba::diag::Nivel::Aviso, "arranque", 0,
                                       ba::i18n::llenar(ba::i18n::traducir("arranque", "la salida «%1» no pudo volver a «%2»: %3"),
                                                        {id, e, AppRuntime::errorDelHilo()}));
    }
    // El negro y la placa vuelven siempre (son de la máquina, no de la corrida): si la app
    // se cerró o se cayó con la placa puesta, vuelve con la placa puesta.
    placaRestaurada_ = restaurarPlacas(aire);
    // Lo que pide la línea de comandos (--record, --stream-server) es parte de volver como
    // estaba: se pide acá, y lo que dice el documento no lo vuelve a pedir (pedidos_).
    if (rt_.cfg_.record) rt_.startRecording();
    if (rt_.cfg_.stream) rt_.startStreaming();
    // VOLVER COMO ESTABA lo que salía: un cierre ordenado deja el documento en «nada al
    // aire» (stop()). Si dice que se transmitía o grababa, la corrida anterior terminó
    // sin cerrarse (corte de luz, cuelgue, la máquina se apagó): se retoma, también en un
    // arranque normal (no solo con --restore del launcher). Con --test (pruebas: el MCP
    // sin aprobaciones) NUNCA se arranca al aire.
    restoreOutputs();
}

bool AireDurable::restaurarPlacas(const salidas::AlAire& aire) {
    bool algo = false;
    for (const auto& [id, s] : aire.salidas) {
        if (!s.placa.puesta() || !rt_.motorPropio().existeSalida(id)) continue;
        const std::string salida = id == salidas::kPorOmision ? std::string() : id;
        if (s.placa.placa && !rt_.motorPropio().placa(true, s.placa.texto, s.placa.imagen, 0, salida))
            ba::diag::registro().linea(ba::diag::Nivel::Error, "arranque", 0,
                                       ba::i18n::llenar(ba::i18n::traducir("arranque", "la placa de la salida «%1» no volvió: %2"),
                                                        {id, rt_.motorPropio().lastError()}));
        if (s.placa.ftb && !rt_.motorPropio().ftb(true, 0, s.placa.ftbAudio, salida))
            ba::diag::registro().linea(ba::diag::Nivel::Error, "arranque", 0,
                                       ba::i18n::llenar(ba::i18n::traducir("arranque", "el negro de la salida «%1» no volvió: %2"),
                                                        {id, rt_.motorPropio().lastError()}));
        ba::diag::registro().linea(ba::diag::Nivel::Aviso, "arranque", 0,
                                   ba::i18n::llenar(ba::i18n::traducir("arranque", "vuelve lo que estaba encima de la salida «%1»: %2"),
                                                    {id, s.placa.placa && s.placa.ftb ? ba::i18n::traducir("arranque", "el negro y la placa")
                                                         : s.placa.placa              ? ba::i18n::traducir("arranque", "la placa")
                                                                                      : ba::i18n::traducir("arranque", "el negro")}));
        algo = true;
    }
    return algo;
}

void AireDurable::restoreOutputs(bool deEstaCorrida) {
    const TurnoMotor::Guarda turno(rt_.motorPropio().turno());
    const salidas::AlAire& aire = aireAnterior_;
    if (aire.cerrado || aire.salidas.empty()) return; // se cerró en orden: nada al aire
    // La escena sí; las salidas de una corrida anterior, nunca con --test (salvo el banco de
    // caos). Las de esta (el rearme por la GPU) estaban saliendo recién: vuelven siempre.
    if (rt_.cfg_.modoPrueba && !rt_.cfg_.retomaEnPrueba && !deEstaCorrida) return;
    // Las grabaciones, cuando se sepa DÓNDE: la carpeta del evento la da la interfaz
    // (setCarpetaDelEvento) después de este arranque; si se retoman antes caen en la
    // carpeta de fábrica, fuera del evento. Sin evento, retomarGrabaciones() a secas.
    grabacionesPorRetomar_ = true;
    if (rt_.emision_->carpetaSabida()) rt_.retomarGrabaciones();
    // TODAS las salidas, cada una con lo suyo: transmisión principal y destinos.
    for (const auto& [id, s] : aire.salidas) {
        if (!rt_.motorPropio().existeSalida(id)) continue;
        const bool po = id == salidas::kPorOmision;
        if (s.transmitiendo && !rt_.emision_->pedidos().de(id).transmitePedida() && !rt_.motorPropio().haySalidaEn(id, /*contarGrabacion=*/false)) {
            ba::diag::registro().linea(ba::diag::Nivel::Aviso, "arranque", 0,
                                       ba::i18n::llenar(ba::i18n::traducir("arranque", "la corrida anterior no se cerró (corte de luz o cuelgue): se retoma la transmisión de la salida «%1»"), {id}));
            // La principal, con su clave de siempre; las demás, con la que usaron (cifrada).
            std::string server, key;
            const bool ok = po ? rt_.startStreaming() : (ServicioEmision::leerUltimaTransmision(server, key, id) && rt_.startStreaming(server, key, false, id));
            if (!ok)
                ba::diag::registro().linea(ba::diag::Nivel::Error, "arranque", 0,
                                           ba::i18n::llenar(ba::i18n::traducir("arranque", "la transmisión de la salida «%1» no se pudo retomar: %2"),
                                                            {id, key.empty() && !po ? ba::i18n::traducir("arranque", "no quedó guardada su clave") : AppRuntime::errorDelHilo()}));
        }
        for (const std::string& destino : s.destinos) {
            ba::diag::registro().linea(ba::diag::Nivel::Aviso, "arranque", 0,
                                       ba::i18n::llenar(ba::i18n::traducir("arranque", "la corrida anterior no se cerró: se retoma el destino «%1» de la salida «%2»"), {destino, id}));
            if (const std::string e = rt_.iniciarDestino(destino); !e.empty())
                ba::diag::registro().linea(ba::diag::Nivel::Error, "arranque", 0,
                                           ba::i18n::llenar(ba::i18n::traducir("arranque", "el destino «%1» no se pudo retomar: %2"), {destino, e}));
        }
    }
}

void AireDurable::rearmarPorGpu() {
    const TurnoMotor::Guarda turno(rt_.motorPropio().turno());
    const auto desde = std::chrono::steady_clock::now();
    const nlohmann::json antes = ba::motor::estadoGpu();
    ba::diag::registro().linea(ba::diag::Nivel::Error, "motor", 0,
                               ba::i18n::llenar(ba::i18n::traducir("arranque", "se perdió la GPU del video (%1): se rearma el motor"),
                                                {antes.value("motivo", std::string())}));
    // Lo que sale ahora es lo que se retoma (como después de un corte, pero sin leer el
    // disco). Las salidas se paran con el motor viejo: lo que estaba saliendo vuelve a
    // empezar (ba-emisor sostiene la transmisión con su placa mientras tanto, F3-01).
    const salidas::AlAire aire = aireActual();
    VideoConfig usado = rt_.cfg_.video;
    ba::motor::Gpu gpu = ba::motor::Gpu::Ninguna;
    const bool ok = rt_.motorPropio().resetVideo(rt_.cfg_.video, [&]() {
        gpu = ba::motor::rearmarGpu();
        usado = gpu == ba::motor::Gpu::Ninguna ? videoParaCpu(rt_.cfg_.video) : rt_.cfg_.video;
        return usado;
    });
    if (!ok) {
        // Sin motor no hay aire: que el supervisor lo relance (el latido de los cuadros se
        // corta solo) y vuelva como de un corte.
        ba::diag::registro().linea(ba::diag::Nivel::Error, "motor", 0,
                                   ba::i18n::llenar(ba::i18n::traducir("arranque", "el motor no se pudo rearmar tras perder la GPU: %1"),
                                                    {rt_.motorPropio().lastError()}));
        // Si no llegó a apagarlo, la marca sigue: que no se reintente en cada tic.
        if (ba::motor::gpuPerdida()) ba::motor::rearmarGpu();
        return;
    }
    rt_.escenas_->reubicarFuentes();
    rt_.operator_->configure(rt_.motorPropio(), rt_.cfg_.operatorVideo, rt_.cfg_.operatorAudio); // lo que ve el lobby, del motor nuevo
    // Las salidas, como restoreOutputs después de un corte: las de otras salidas se vuelven a
    // anotar al arrancar (startRecording/startStreaming). Lo que cerró el corte de luz NO se
    // retoma acá (la batería sigue baja): sigue esperando la energía.
    aireAnterior_ = aire;
    aireAnterior_.cerrado = false;
    for (const std::string& id : rt_.emision_->pedidos().rearmar())
        if (const auto it = aireAnterior_.salidas.find(id); it != aireAnterior_.salidas.end()) it->second.grabando = false;
    restaurarPlacas(aire);
    restoreOutputs(/*deEstaCorrida=*/true);
    writeOnAirState();
    rt_.avisar(Cambio::Salidas);
    rt_.avisar(Cambio::Escenas);
    const long long ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - desde).count();
    ba::diag::registro().linea(ba::diag::Nivel::Aviso, "motor", 0,
                               ba::i18n::llenar(ba::i18n::traducir("arranque", "motor rearmado tras perder la GPU en %1 ms: video en %2, %3×%4"),
                                                {std::to_string(ms), ba::motor::nombreGpu(), std::to_string(usado.baseWidth),
                                                 std::to_string(usado.baseHeight)}));
}

} // namespace ba::app
