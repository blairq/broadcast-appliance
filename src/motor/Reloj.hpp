// ============================================================================
// src/motor/Reloj.hpp — el reloj del estudio y la latencia del estudio
//
// TODOS los pipelines del motor (entradas, mezcla, composición, salidas) usan
// el mismo reloj y el mismo base_time: así la hora de ejecución de un cuadro o
// un bloque de audio vale igual en cualquiera de ellos (lo capturado a la vez
// tiene la misma hora) y `intersink`/`intersrc` pasan las horas tal cual.
//
// Una cadena que no puede quedarse con el base_time del estudio (un archivo:
// cada salto con flush y cada pausa lo cambian) corrige con un offset en el pad
// de salida del elemento que entrega (base de la cadena − base del estudio): sus
// horas quedan en las del estudio (ver Entrada::corregirOffset).
//
// LA LATENCIA DEL ESTUDIO: cada entrada anota lo que tarda (declarado por su
// cadena o medido); la del estudio es la mayor de las que no pasan el TOPE (de
// fábrica 250 ms). Una que pasa el tope «llega tarde»: no arrastra a las demás.
// Los mezcladores esperan esa latencia (más un margen).
// ============================================================================
#pragma once

#include <gst/gst.h>

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>

namespace ba::motor {

/// Inicia GStreamer con el entorno del bundle (una vez). false + motivo si no hay.
bool iniciarGStreamer(std::string* error = nullptr);
/// Lo mismo, pero gst_init corre en otro hilo y este llama a `esperando` cada
/// ~100 ms hasta que termine (el arranque: que el cartel de carga se repinte y la
/// app siga latiendo mientras GStreamer rehace su lista de plugins, que la primera
/// vez en una PC puede tardar minutos; ver platform/GstEntorno.hpp).
bool iniciarGStreamer(std::string* error, const std::function<void()>& esperando);
/// Mientras GStreamer rehace su lista de plugins (la primera vez en una PC): cuántos
/// va a revisar, en cuál va (1 = el primero) y cuál es (el archivo). total == 0:
/// no está rehaciéndola.
struct ProgresoGst {
    int total = 0;
    int revisados = 0;
    std::string actual;
};
ProgresoGst progresoGStreamer();
/// Saca del registro los plugins que meten código del sistema en el proceso
/// (DirectShow: le ofrece a decodebin cualquier códec instalado, como los LAV
/// del K-Lite). Por si quedó el .dll de una versión vieja. Después de gst_init.
void quitarPluginsDelSistema();
/// Los convertidores de D3D12 se crean de a uno (d3d12convert/scale/colorconvert:
/// su set_caps, bajo un candado de todo el proceso). GStreamer 1.26 comparte entre
/// todos un blob de D3D12 (la firma raíz del mipgen) y le suma y resta referencias
/// sin protección: con muchas placas negociando a la vez se libera dos veces y se
/// rompe el heap (0xC0000374, las caídas del 2026-10-09/10/12; se reproduce con
/// gst-launch solo: 32 ramas, 1 de cada ~300 arranques). Después de gst_init; una vez.
void serializarConvertidoresD3D12();

enum class Nivel { Info, Aviso, Error };
/// Adónde van los avisos del motor (de fábrica, stderr). La app pone su registro.
void ponerRegistro(std::function<void(Nivel, const std::string&)> f);
void registrar(Nivel n, const std::string& texto);

class Reloj {
   public:
    /// Cuánto se espera que un reloj de red (NTP, PTP, otro aparato) se sincronice antes de
    /// tomar la hora base; si no, el del sistema.
    static constexpr GstClockTime kEsperaSyncNs = 3 * GST_SECOND;
    /// `tipo`: sistema (de fábrica), ntp, ptp o red; `servidor`/`puerto`/`dominio` según el tipo.
    explicit Reloj(const std::string& tipo = "sistema", const std::string& servidor = "", int puerto = 0,
                   int dominio = 0);
    ~Reloj();
    Reloj(const Reloj&) = delete;
    Reloj& operator=(const Reloj&) = delete;

    bool valido() const { return clock_ != nullptr; }
    const std::string& error() const { return error_; }
    GstClock* clock() const { return clock_; }
    GstClockTime base() const { return base_; }
    /// La hora de ejecución del estudio ahora.
    GstClockTime ahora() const;

    /// El pipeline usa el reloj y el base_time del estudio (y no lo cambia solo).
    void adoptar(GstElement* pipeline) const;

    // --- La latencia del estudio ---
    /// -1 = automático (250 ms).
    void ponerTope(std::int64_t ms);
    std::int64_t topeMs() const;
    bool topeAutomatico() const;
    /// Lo que pide `quien` (ns). Devuelve la del estudio (ns).
    std::int64_t anotar(const std::string& quien, std::int64_t ns);
    void olvidar(const std::string& quien);
    /// La latencia del estudio (ns): la FIJADA. No sigue cada medición: sube si lo que piden
    /// las entradas sincronizadas lo pide sostenido (revisarLatencia) y baja solo con permiso
    /// (sin nada al aire) o a pedido (bajarLatencia). Cambiarla en vivo mueve la línea de tiempo
    /// de todas las salidas (congela o saltea): por eso no se mueve con cada corte.
    std::int64_t latencia() const;
    /// Lo que piden ahora (ns): la mayor de las anotadas que no pasan el tope.
    std::int64_t objetivo() const;
    /// Quién pide el objetivo ("" si nadie).
    std::string laMarca() const;
    /// Una vez por vuelta del motor: sube si el objetivo pasa la fijada hace kSostenerSubidaNs;
    /// con `puedeBajar`, baja si queda por debajo (más de 20 ms) hace kSostenerBajadaNs.
    /// Devuelve true si cambió (para avisar).
    bool revisarLatencia(bool puedeBajar);
    /// Bajar ahora a lo que piden (el operador lo pide).
    bool bajarLatencia();
    /// Si hay una bajada esperando permiso (la fijada está arriba de lo que piden).
    bool puedeBajarse() const;
    /// Para las pruebas: da por terminado el arranque (los primeros segundos sigue al objetivo).
    void saltearArranque();
    static constexpr std::int64_t kSostenerSubidaNs = 2 * GST_SECOND;
    static constexpr std::int64_t kSostenerBajadaNs = 10 * GST_SECOND;
    /// ¿Pasa el tope?
    bool llegaTarde(std::int64_t ns) const;
    /// Las anotadas: quien → ns.
    std::map<std::string, std::int64_t> anotadas() const;

   private:
    GstClock* clock_ = nullptr;
    GstClockTime base_ = 0;
    std::string error_;
    mutable std::mutex mu_;
    std::int64_t topeMs_ = -1;
    std::map<std::string, std::int64_t> pedidas_;
    std::int64_t fijada_ = -1;              ///< -1 = todavía no (arranque: sigue al objetivo)
    std::int64_t subeDesde_ = 0, bajaDesde_ = 0;
    std::int64_t arranque_ = 0;
    std::int64_t objetivoSinLock() const;
};

constexpr std::int64_t kTopeAutomaticoMs = 250;

} // namespace ba::motor
