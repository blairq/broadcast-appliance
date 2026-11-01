// ============================================================================
// src/app/emision/SalidaMotor.hpp — una salida del motor, partida por hilo dueño
//
// Por dentro de BroadcastEngine (ver LAS SALIDAS en BroadcastEngine.hpp). Una salida tiene
// dos mitades, cada una con UN hilo dueño, que se hablan por mensajes:
//
//   - SalidaAire: lo del aire y lo pedido. Del DUEÑO DEL MOTOR (el que tiene el turno: el
//     plano de control). La escena al aire y la armada, las capas (DSK, placa, negro), su
//     mezcla, lo que se le pidió (transmitir, grabar, limpia, destinos: para contestar «ya
//     está» sin esperar) y su retorno al gateway.
//   - SalidaEmision: lo que sale. Del HILO DE SALIDAS mientras anda (EmisionSalidas; antes de
//     arrancarlo y después de pararlo, del dueño): las transmisiones, la grabación con su
//     copia, la repetición, la señal limpia y lo del emisor aparte.
//
// El dueño le habla al hilo de salidas con órdenes encoladas (EmisionSalidas::enSalidas: la
// SalidaEmision va en la orden). El hilo de salidas le contesta al dueño por el BUZÓN
// (BuzonSalidas): «no arrancó» (lo pedido baja, si sigue siendo esa orden), «adopté lo que
// el emisor ya tenía» (lo pedido sube), «transmito AAC a tantos kbps» (el techo del
// loudness). El dueño lo atiende cada vez que va a leer lo pedido y en cada vuelta del
// motor (BroadcastEngine::atenderSalidas). Lo que lee la interfaz (lo que sale de verdad) es
// la foto de las salidas que publica el hilo de salidas (fotoSalidas, con su candado).
//
// Lo que es de los dos y no cambia después de crearla: id, nombre (en SalidaAire), emisor.
// Las medidas (Medidas) son de los hilos del motor que las alimentan y se leen desde
// cualquier hilo: todo atómico o con su candado.
// ============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "audio/LoudnessPrograma.hpp"
#include "emision/BroadcastEngine.hpp"
#include "emision/BuzonSalidas.hpp"
#include "ipc/SpoutTransport.hpp"

namespace ba::app {

/// Lo que se mide del audio y del video del programa de una salida (llega de los hilos del
/// motor): aparte, compartido con quien lo escucha, para que una salida borrada no lo deje
/// colgado. DESDE CUALQUIER HILO (atómicos; el loudness tiene su candado).
struct MedidasSalida {
    audio::LoudnessPrograma loudness{48000};
    std::atomic<float> picoL{0.0f}, picoR{0.0f};
    /// El mismo pico para el vigía de las alarmas (picoProgramaVigiaDb): cada lector
    /// pone el suyo en cero, así ninguno le roba el pico al otro.
    std::atomic<float> picoVigia{0.0f};
    // Lo que llega de su programa en video (claveAlarmas): brillo medio, la firma del
    // último cuadro, desde cuándo no cambia y cuántos salieron.
    std::atomic<double> luma{0};
    std::atomic<std::uint64_t> firma{0};
    std::atomic<std::int64_t> cambioMs{0};
    std::atomic<std::uint64_t> cuadros{0};
};

/// Una capa DSK de una salida (1..4).
struct DskSalida {
    std::vector<std::string> contenido;
    nlohmann::json recuadro = nlohmann::json::object();
    bool visible = false;
};

/// El retorno de una salida al gateway (RetornoGateway): el canal lo publica el dueño y los
/// cuadros los manda el hilo del motor, bajo `mu`.
struct RetornoDeSalida {
    std::string id;
    bool porOmision = false;
    std::mutex mu;
    ba::ipc::SpoutSender canal;
    int ancho = 0, alto = 0; ///< el tamaño con que se publicó
    std::uint64_t cuadros = 0;
    std::vector<std::uint8_t> filas;
    bool pedido = false; ///< del dueño: su vista «…@gateway» está pedida
    bool enUso = false;  ///< del dueño: alguien lo mira (ver BroadcastEngine::retornoEnUso)
    /// La vista de su programa para el gateway y el nombre de su canal.
    std::string clave() const;
    std::wstring nombreCanal() const;
};

/// El canal de video de una salida a su emisor aparte (EmisorAparte): el escritor y la vista
/// que lo alimenta. El de su programa es del dueño; el de la señal limpia, del hilo de
/// salidas. Los cuadros los manda el hilo del motor, bajo `mu`.
struct CanalEmisor {
    std::mutex mu; ///< tx, el tamaño y las filas
    ba::ipc::SpoutSender tx;
    int ancho = 0, alto = 0;
    std::uint64_t cuadros = 0;
    std::vector<std::uint8_t> filas;
    std::string clave; ///< la vista del motor («programa…@emisor», «limpio…@emisor»)
    bool pedida = false;
};

/// Una grabación (o transmisión) que no es la del programa: una entrada aparte (ISO) o la
/// señal limpia. Del hilo de salidas.
struct GrabacionAparte {
    std::string path;
    int kbps = 0; ///< lo que escribe a disco (video + audio), para las horas restantes
    bool audioPropio = false;
    std::unique_ptr<motor::Grabacion> grab;
    std::unique_ptr<motor::Transmision> tx;
    std::string destino;
    GrabacionAparte();
    GrabacionAparte(GrabacionAparte&&) noexcept;
    GrabacionAparte& operator=(GrabacionAparte&&) noexcept;
    ~GrabacionAparte();
};

struct SalidaEmision;

/// LO DEL AIRE de una salida. Del dueño del motor (con el turno).
struct SalidaAire {
    std::string id, nombre;
    bool emisor = false; ///< la codifica un ba-emisor (BA_EMISOR): fijo desde que se crea
    std::string alAire;  ///< la escena al aire
    std::string armada;  ///< la escena en su PREVIEW (ver BroadcastEngine::armar)
    DskSalida dsk[4];
    bool ftb = false, ftbAudio = false, placa = false;
    std::string placaTexto, placaImagen;
    /// El volumen y el mute de cada fuente en su mezcla (la por omisión usa los de la fuente:
    /// estos quedan vacíos).
    std::map<std::string, float> volumen;
    std::map<std::string, bool> mute;
    std::map<std::string, float> gananciaGrupo; ///< los VCA de su mezcla (solo los que no son 1)
    RecordConfig lastRec;
    bool lastRecValido = false;
    int pistasGrabacionMask = 0;
    std::shared_ptr<MedidasSalida> medidas = std::make_shared<MedidasSalida>();
    int oyenteAudio = 0; ///< el oyente del motor que la mide (VistasYMedicion)
    // --- Lo pedido al hilo de salidas (ver el buzón) ---
    PedidoOrden stream, grabacion, limpio;
    std::map<std::string, std::uint64_t> extras; ///< destino -> orden que lo pidió
    std::string activeServer, activeKey;          ///< a dónde se pidió transmitir
    /// El AAC más chico con que se transmite por acá (0: ninguno; lo cuenta el hilo de
    /// salidas por el buzón): debajo de 128 kbps el techo baja a −2 dBTP (aplicarLoudness).
    int kbpsAac = 0;
    std::shared_ptr<RetornoDeSalida> retorno;
    std::shared_ptr<CanalEmisor> canalEmisor; ///< su programa al emisor aparte
    /// Su mitad en el hilo de salidas: va en las órdenes que se le encolan.
    std::shared_ptr<SalidaEmision> emision;

    bool porOmision() const { return id == BroadcastEngine::kSalidaPorOmision; }
};

/// LO QUE SALE de una salida. Del hilo de salidas (EmisionSalidas).
struct SalidaEmision {
    struct Extra {
        std::string server, key, serverRespaldo;
        std::unique_ptr<motor::Transmision> tx;
    };
    /// La repetición: un anillo de tramos chicos en una carpeta; guardar junta los últimos.
    struct Repeticion {
        std::unique_ptr<motor::Repeticion> rep;
        std::string dir;
        mutable std::mutex mu; ///< ultimo (lo escribe el hilo que guarda el clip)
        std::string ultimo;
        std::atomic<bool> guardando{false};
    };

    std::string id;
    bool emisor = false;
    std::unique_ptr<motor::Transmision> stream;
    std::map<std::string, Extra> extras;
    std::unique_ptr<motor::Grabacion> rec, recCopia;
    std::string recordPath, recordCopyPath;
    std::string fuenteVideo; ///< el productor del video de su programa: la lista negra de codificadores es suya
    int kbpsRec = 0;         ///< lo que escriben la grabación y (otro tanto) la copia (kbpsGrabacion)
    int kbpsAac = 0;         ///< el AAC más chico con que se transmitió (se le cuenta al dueño)
    std::string remuxSal;    ///< a qué pasar la grabación al pararla
    bool remuxAlTerminarSal = true;
    std::unique_ptr<GrabacionAparte> clean;
    std::unique_ptr<Repeticion> replay;
    /// Por qué no arrancó la última orden de grabar / transmitir ("" = arrancó) y si lo que
    /// arrancó sigue (lo contesta a un segundo pedido de lo mismo: contestarComoLaAnterior).
    std::string falloGrabacion, falloTransmision;
    bool grabacionViva = false, transmisionViva = false;
    // --- El emisor aparte (F3-01; EmisorAparte) ---
    // Lo pedido (copia de Runtime/emisor/<id>.json) y lo último que dijo su estado (con
    // cuándo se leyó: se relee cada medio segundo).
    std::unique_ptr<emisor::Pedido> pedidoEmisor;
    nlohmann::json estadoEmisor;
    std::int64_t estadoEmisorMs = 0;
    /// Lo que su pedido ya tenía (destinos, señal limpia, pistas) está enganchado acá en
    /// esta corrida del motor (sus canales, sus puentes, lo pedido): ver EmisorAparte::leer.
    bool emisorEnganchado = false;
    std::shared_ptr<CanalEmisor> canalLimpioEmisor; ///< la señal limpia al emisor aparte

    SalidaEmision();
    ~SalidaEmision(); // en EmisionSalidas.cpp: los tipos del motor se completan ahí
    bool porOmision() const { return id == BroadcastEngine::kSalidaPorOmision; }
};

} // namespace ba::app
