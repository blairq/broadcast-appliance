// ============================================================================
// src/gateway/TopeRetorno.hpp — la resolución máxima del video de retorno de toda la sala
//
// EL PROBLEMA (medido el 2026-11-05 en la PC de pruebas, 200 invitados por TURN): con la red
// del que mira por cable, la estimación de ancho de banda de cada conexión ya no frena y 157
// de 200 reciben el retorno en 720p. El gateway no da abasto (1135 % de CPU, la máquina al
// 86-92 %): el retorno sale a 6,3 cps de los 30, las celdas de la grilla caen a 5,7 cps y la
// pérdida sube al 3,3 %. La red de cada uno ya no es el límite: es la máquina.
//
// EL TOPE: un ajuste del evento, `retorno.tope` (Configuración → Conferencia):
//   - «automático» (de fábrica): lo decide el regulador de acá abajo, entre los escalones
//     720p → 480p → 360p (o desde la capa más alta de `retorno.capas`, si es otra);
//   - «360p», «480p», «720p», «1080p»: fijo.
// Se aplica UNA vez para toda la sala, en las calidades del retorno compartido
// (RetornoCompartido.hpp, capasConTope): con el tope en 480p, las calidades son 480p, 360p y
// 180p; cada una se sigue codificando una sola vez para todos los que la reciben. Lo de cada
// conexión no cambia: su control de congestión elige entre las calidades que hay, y el tope
// del operador para esa persona (calidad-360p) sigue mandando si es más bajo.
//
// EL REGULADOR (automático), una vez por segundo, con lo que el gateway ya mide:
//   - la CPU de la máquina (la del gateway se informa, pero no alcanza sola: la app comparte
//     la máquina y lo que le falta al gateway es lo que deja libre el total);
//   - los cps que logra el retorno contra los que debería: los cuadros que salen hacia cada
//     invitado (los que entrega el codificador de paso) contra los que la fuente le dio;
//   - si el codificador del retorno llega a tiempo: los cuadros que descarta (cola llena o
//     sin respuesta) contra los que codifica;
//   - cuántos invitados reciben el retorno (los oyentes).
// Con el promedio de los últimos 3 segundos:
//   - NO LLEGA si el codificador descarta `descartesAltoPct` o más, o si el retorno sale a
//     menos de `cpsBajo` de lo que debería CON la máquina en `cpuAlta` o más (con la máquina
//     holgada, un retorno lento es la red de alguien: eso lo resuelve su control de
//     congestión, no el tope de todos). Sostenido `sostenerBajarMs`: baja un escalón.
//   - SOBRA MARGEN si el retorno sale a `cpsBien` o más (o no hay oyentes), la máquina está
//     en `cpuBaja` o menos (o no se sabe) y el codificador no descarta. Sostenido
//     `sostenerSubirMs` (bastante más que bajar: subir es prudente): sube un escalón.
//   - Entre medio, se queda (la histéresis). Entre dos cambios pasan al menos `esperaMs`
//     (que las conexiones lleguen a cambiar de calidad y la medida lo refleje).
//   - LA GENTE: al bajar de un escalón se anota con cuántos oyentes no llegaba. Volver a
//     subir a ese escalón con la misma gente (o más) espera `reintentarMs`, que se duplica
//     cada vez que al subir vuelve a no llegar enseguida (hasta `reintentarMaxMs`); con
//     menos gente (menos del 80 %) sube apenas sobra margen. Así no oscila.
// Cada cambio queda en el registro del gateway con el motivo y la medida que lo decidió, y
// en el estado de la sala (`retornoTope`: lo leen la app y el MCP room_status).
//
// Sin hilos ni red: lo usa SignalingServer en su hilo de la carga y se prueba solo
// (test_tope_retorno). No depende de la plataforma: la CPU la mide CpuMaquina.
// ============================================================================
#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::gateway {

/// Lo medido en el último segundo (-1 = no se sabe).
struct MedidaRetorno {
    double cpuMaquina = -1;   ///< % de toda la máquina
    double cpuGateway = -1;   ///< % de la máquina que usa el gateway (se informa)
    double cpsLogrado = -1;   ///< cuadros por segundo que salen hacia cada oyente (promedio)
    double cpsDebido = -1;    ///< los que la fuente le dio a cada oyente (promedio)
    double descartesPct = -1; ///< de lo que se le pidió al codificador del retorno, lo descartado
    int oyentes = 0;          ///< invitados que reciben el retorno
};

/// Los ajustes del regulador. El modo sale de `retorno.tope`; los umbrales son de fábrica.
struct AjustesTope {
    int fijo = 0;                          ///< 0 = automático; si no, el alto fijo
    std::vector<int> escalones{720, 480, 360}; ///< automático: de arriba abajo (escalonesDesde)
    double cpuAlta = 80;
    double cpuBaja = 65;
    double cpsBajo = 0.7;  ///< logrado/debido por debajo de esto (con la máquina alta): no llega
    double cpsBien = 0.9;  ///< por encima de esto: llega
    double descartesAltoPct = 10;
    double descartesBienPct = 1;
    std::int64_t sostenerBajarMs = 3000;
    std::int64_t sostenerSubirMs = 20000;
    std::int64_t esperaMs = 5000;
    std::int64_t reintentarMs = 60000;
    std::int64_t reintentarMaxMs = 600000;
};

/// «auto» | «360p» | «480p» | «720p» | «1080p» (el valor de `retorno.tope`) → el alto fijo
/// (0 = automático; lo que no se entiende, automático).
int altoDeAjusteTope(const std::string& valor);
/// Los escalones del automático: 1080, 720, 480 y 360 que no pasen `altoMax` (la capa más
/// alta de `retorno.capas`); si `altoMax` no es uno de ellos, va primero.
std::vector<int> escalonesDesde(int altoMax);

class ReguladorRetorno {
   public:
    /// Por qué está el tope donde está.
    enum class Causa { Inicio, Fijo, Cps, Codificador, Margen };
    static const char* nombreCausa(Causa c);

    void ajustes(const AjustesTope& a);
    const AjustesTope& ajustes() const { return a_; }

    /// Una vuelta con lo medido en `ahoraMs`. true si cambió el tope (o se cambió el modo: la
    /// primera vuelta, o Configuración), para anotarlo en el registro con motivo().
    bool medir(const MedidaRetorno& m, std::int64_t ahoraMs);

    /// El alto que rige (nunca 0: en automático, el escalón; fijo, el fijo).
    int tope() const;
    bool automatico() const { return a_.fijo <= 0; }
    Causa causa() const { return causa_; }
    /// Cuántas veces cambió (el tope, el modo o la causa): el que publica el estado en la sala
    /// sabe así si tiene que esperarla (un cambio) o si es solo la medida de cada segundo.
    std::uint64_t cambios() const { return cambios_; }
    /// La medida que decidió el último cambio (promediada).
    const MedidaRetorno& medidaDelCambio() const { return medidaCambio_; }
    /// El promedio de los últimos segundos (lo que se está mirando).
    MedidaRetorno promedio() const;
    /// Una línea para el registro (en el idioma del gateway): el cambio y su motivo.
    std::string motivo() const;

    /// {modo: "auto"|"fijo", alto, causa, motivo, desdeMs, medida:{cpu_maquina, cpu_gateway,
    ///  cps, cps_debido, descartes_pct, oyentes}, decision:{…la del último cambio},
    ///  escalones, decisiones:[{ms, de, a, causa, motivo}] (con `historial`)}.
    nlohmann::json estado(bool historial) const;

   private:
    void cambiar(int escalon, Causa c, const MedidaRetorno& m, std::int64_t ahoraMs);

    AjustesTope a_;
    int escalon_ = 0; ///< índice en a_.escalones
    int fijoAntes_ = -1;
    Causa causa_ = Causa::Inicio;
    MedidaRetorno medidaCambio_;
    std::deque<MedidaRetorno> ventana_; ///< las últimas 3
    std::int64_t presionDesde_ = 0;     ///< desde cuándo no llega (0 = no)
    std::int64_t margenDesde_ = 0;      ///< desde cuándo sobra (0 = no)
    std::int64_t ultimoCambio_ = 0;
    std::int64_t desdeMs_ = 0;
    std::uint64_t cambios_ = 0;
    /// Por escalón: con cuántos oyentes no llegaba al bajar de él, cuándo y cuánto esperar
    /// para volver a probarlo con la misma gente.
    struct Recuerdo {
        int oyentes = -1;
        std::int64_t bajoMs = 0;
        std::int64_t esperarMs = 0;
        std::int64_t subioMs = 0; ///< la última vez que se volvió a él
    };
    std::vector<Recuerdo> recuerdos_;
    struct Decision {
        std::int64_t ms = 0;
        int de = 0, a = 0;
        Causa causa = Causa::Inicio;
        std::string motivo;
    };
    std::deque<Decision> decisiones_;
};

} // namespace ba::gateway
