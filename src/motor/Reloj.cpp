// ============================================================================
// src/motor/Reloj.cpp — ver Reloj.hpp
// ============================================================================
#include "Reloj.hpp"
#include "diag/Utf8.hpp"
#include "Mapa.hpp"
#include "Piezas.hpp"

#include "platform/GstEntorno.hpp"

#include <gst/base/gstbasetransform.h>
#include <gst/net/net.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace ba::motor {

namespace {
std::mutex g_regMu;
std::function<void(Nivel, const std::string&)> g_registro;
} // namespace

namespace {
// --- La revisión de plugins (cuando GStreamer rehace su registro) -------------
// Cuál está revisando sale del archivo de diagnóstico del escáner (lo que anota
// él al empezar cada uno: la app no lo sabe, porque en macOS y Linux le manda
// todos de una y las respuestas llegan con un plugin de atraso). Los que no
// contestaron, del proceso de la app.
std::string archivoDe(std::string ruta) {
    const auto barra = ruta.find_last_of("/\\");
    if (barra != std::string::npos) ruta = ruta.substr(barra + 1);
    return ruta;
}
/// Lo que sigue a `prefijo` hasta `fin`, si `t` empieza con `prefijo`.
bool entre(const std::string& t, const std::string& prefijo, const char* fin, std::string& sale) {
    if (t.rfind(prefijo, 0) != 0) return false;
    const auto f = t.find(fin, prefijo.size());
    if (f == std::string::npos) return false;
    sale = t.substr(prefijo.size(), f - prefijo.size());
    return true;
}

using Hora = std::chrono::steady_clock::time_point;
struct Revisado {
    std::string archivo;
    Hora desde; // cuándo se vio que el escáner lo empezó a cargar
};
struct Revision {
    std::mutex mu;
    int total = 0;
    Hora desde;
    // Lo que va anotando cada escáner (archivo de diagnóstico → cuánto se leyó y
    // la línea a medias). El escáner revisa de a un plugin: el último que empezó
    // es el que está revisando.
    std::map<std::filesystem::path, std::pair<std::uintmax_t, std::string>> leido;
    std::vector<Revisado> revisados;
    std::vector<std::string> fallados;

    /// Lee lo nuevo de los archivos de los escáneres (bajo `mu`).
    void leer() {
        const Hora ahora = std::chrono::steady_clock::now();
        static const std::string kEmpieza = "attempt to load plugin \"";
        for (const auto& ruta : ba::platform::archivosDeEscaneres()) {
            auto& [desde, resto] = leido[ruta];
            std::ifstream f(ruta, std::ios::binary);
            if (!f) continue;
            f.seekg(static_cast<std::streamoff>(desde));
            std::string nuevo((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            desde += nuevo.size();
            resto += nuevo;
            std::size_t ini = 0;
            for (std::size_t fin; (fin = resto.find('\n', ini)) != std::string::npos; ini = fin + 1) {
                const auto i = resto.find(kEmpieza, ini);
                if (i == std::string::npos || i > fin) continue;
                const auto cierra = resto.find('"', i + kEmpieza.size());
                if (cierra == std::string::npos || cierra > fin) continue;
                revisados.push_back({archivoDe(resto.substr(i + kEmpieza.size(), cierra - i - kEmpieza.size())), ahora});
            }
            resto.erase(0, ini);
        }
    }
};
Revision& revision() {
    static Revision r;
    return r;
}

// Los que no contestaron los dice el proceso de la app (no el escáner, que se
// trabó): se los oye con un registrador propio mientras dura el gst_init.
void oirRevision(GstDebugCategory* cat, GstDebugLevel, const gchar*, const gchar*, gint, GObject*,
                 GstDebugMessage* m, gpointer) G_GNUC_NO_INSTRUMENT;
void oirRevision(GstDebugCategory* cat, GstDebugLevel, const gchar*, const gchar*, gint, GObject*,
                 GstDebugMessage* m, gpointer) {
    const char* c = gst_debug_category_get_name(cat);
    const gchar* texto = gst_debug_message_get(m);
    if (c == nullptr || texto == nullptr || std::strcmp(c, "GST_PLUGIN_LOADING") != 0) return;
    const std::string t(texto);
    std::string archivo;
    // Windows: «Loading plugin X failed»; macOS/Linux: «Plugin file X failed to load».
    if (!entre(t, "Loading plugin ", " failed", archivo) && !entre(t, "Plugin file ", " failed to load", archivo)) return;
    Revision& r = revision();
    std::lock_guard<std::mutex> l(r.mu);
    archivo = archivoDe(archivo);
    if (std::find(r.fallados.begin(), r.fallados.end(), archivo) == r.fallados.end()) r.fallados.push_back(archivo);
}

void empezarRevision(int total) {
    Revision& r = revision();
    {
        std::lock_guard<std::mutex> l(r.mu);
        r.total = total;
        r.desde = std::chrono::steady_clock::now();
        r.leido.clear();
        r.revisados.clear();
        r.fallados.clear();
    }
    gst_debug_add_log_function(oirRevision, nullptr, nullptr);
}

/// Deja de oír y anota en el registro cuánto tardó, cuáles tardaron y cuáles fallaron.
void terminarRevision() {
    gst_debug_remove_log_function(oirRevision);
    Revision& r = revision();
    std::lock_guard<std::mutex> l(r.mu);
    r.leer();
    const Hora ahora = std::chrono::steady_clock::now();
    const auto seg = [](std::chrono::steady_clock::duration d) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(d).count() / 1000.0;
    };
    char b[160];
    std::snprintf(b, sizeof b, "GStreamer rehízo su lista de plugins en %.1f s: revisó %zu de %d", seg(ahora - r.desde),
                  r.revisados.size(), r.total);
    registrar(Nivel::Info, std::string(b) + " (el detalle, en Logs/gst-plugins-*.log)");
    // Lo que tardó cada uno: hasta que empezó el siguiente (se mira cada ~100 ms).
    std::vector<std::pair<double, std::string>> lentos;
    for (std::size_t i = 0; i < r.revisados.size(); ++i) {
        const Hora fin = i + 1 < r.revisados.size() ? r.revisados[i + 1].desde : ahora;
        const double s = seg(fin - r.revisados[i].desde);
        if (s >= 3.0) lentos.emplace_back(s, r.revisados[i].archivo);
    }
    std::sort(lentos.rbegin(), lentos.rend());
    for (const auto& [s, archivo] : lentos) {
        std::snprintf(b, sizeof b, "%.1f s", s);
        registrar(Nivel::Aviso, "el plugin " + archivo + " tardó " + b + " en revisarse");
    }
    for (const std::string& archivo : r.fallados)
        registrar(Nivel::Aviso, "el plugin " + archivo +
                                    " no contestó al revisarlo: queda afuera (GStreamer lo anota y no lo vuelve a cargar)");
    r.total = 0;
}
} // namespace

ProgresoGst progresoGStreamer() {
    Revision& r = revision();
    std::lock_guard<std::mutex> l(r.mu);
    ProgresoGst p;
    p.total = r.total;
    if (r.total == 0) return p;
    r.leer();
    p.revisados = static_cast<int>(r.revisados.size());
    if (!r.revisados.empty()) p.actual = r.revisados.back().archivo;
    return p;
}

bool iniciarGStreamer(std::string* error) {
    static std::once_flag una;
    static bool ok = false;
    static std::string err;
    std::call_once(una, []() {
        if (gst_is_initialized()) {
            ok = true;
            serializarConvertidoresD3D12();
            return;
        }
        if (!ba::platform::prepararGst()) {
            err = "GStreamer no está en el programa";
            return;
        }
        // Si rehace la lista de plugins, se oye cuál revisa (progresoGStreamer) y al
        // final queda en el registro cuáles tardaron o fallaron.
        const char* gstDebug = std::getenv("GST_DEBUG");
        const bool delUsuario = gstDebug != nullptr && *gstDebug != '\0';
        const bool rehacer = ba::platform::empezarRegistro();
        if (rehacer) empezarRevision(ba::platform::contarPlugins());
        GError* e = nullptr;
        ok = gst_init_check(nullptr, nullptr, &e);
        if (!ok) err = e != nullptr ? ba::diag::utf8Valido(e->message) : "gst_init falló";
        if (e != nullptr) g_error_free(e);
        if (rehacer) {
            if (!delUsuario) {
                gst_debug_set_threshold_for_name("GST_PLUGIN_LOADING", GST_LEVEL_NONE);
            }
            terminarRevision();
        }
        ba::platform::registroTerminado(ok);
        if (ok) {
            quitarPluginsDelSistema();
            serializarConvertidoresD3D12();
        }
    });
    if (!ok && error != nullptr) *error = err;
    return ok;
}

bool iniciarGStreamer(std::string* error, const std::function<void()>& esperando) {
    if (!esperando) return iniciarGStreamer(error);
    // En otro hilo; este sigue llamando a `esperando` hasta que termine. Si ya
    // estaba iniciado, el hilo vuelve enseguida.
    struct Estado {
        std::mutex mu;
        std::condition_variable cv;
        bool listo = false;
    };
    auto estado = std::make_shared<Estado>();
    std::thread hilo([estado]() {
        iniciarGStreamer(nullptr);
        std::lock_guard<std::mutex> l(estado->mu);
        estado->listo = true;
        estado->cv.notify_all();
    });
    for (;;) {
        {
            std::unique_lock<std::mutex> l(estado->mu);
            if (estado->cv.wait_for(l, std::chrono::milliseconds(100), [&] { return estado->listo; })) break;
        }
        esperando();
    }
    hilo.join();
    return iniciarGStreamer(error); // ya iniciado: el resultado guardado
}

void quitarPluginsDelSistema() {
    GstRegistry* reg = gst_registry_get();
    for (const char* n : {"directshow"}) {
        if (GstPlugin* p = gst_registry_find_plugin(reg, n)) {
            gst_registry_remove_plugin(reg, p);
            gst_object_unref(p);
        }
    }
}

namespace {
using SetCaps = gboolean (*)(GstBaseTransform*, GstCaps*, GstCaps*);
struct ConvertidorD3D12 {
    const char* fabrica;
    GType tipo = 0;
    SetCaps original = nullptr;
};
// Se llenan una vez (serializarConvertidoresD3D12), antes de que exista ninguno.
ConvertidorD3D12 g_d3d12[] = {{"d3d12convert"}, {"d3d12scale"}, {"d3d12colorconvert"}};
std::recursive_mutex g_d3d12Mu;

gboolean setCapsDeAUno(GstBaseTransform* t, GstCaps* entra, GstCaps* sale) {
    for (const auto& c : g_d3d12)
        if (c.original != nullptr && G_OBJECT_TYPE(t) == c.tipo) {
            std::lock_guard<std::recursive_mutex> l(g_d3d12Mu);
            return c.original(t, entra, sale);
        }
    return FALSE;
}
} // namespace

void serializarConvertidoresD3D12() {
    static std::once_flag una;
    std::call_once(una, []() {
        for (auto& c : g_d3d12) {
            GstElementFactory* f = gst_element_factory_find(c.fabrica);
            if (f == nullptr) continue; // sin D3D12 (macOS, Linux)
            auto* cargada = GST_ELEMENT_FACTORY(gst_plugin_feature_load(GST_PLUGIN_FEATURE(f)));
            gst_object_unref(f);
            if (cargada == nullptr) continue;
            const GType tipo = gst_element_factory_get_element_type(cargada);
            gst_object_unref(cargada);
            if (tipo == 0 || !g_type_is_a(tipo, GST_TYPE_BASE_TRANSFORM)) continue;
            // La clase queda viva (la referencia no se suelta): el puntero cambiado dura.
            auto* k = static_cast<GstBaseTransformClass*>(g_type_class_ref(tipo));
            if (k->set_caps == nullptr || k->set_caps == setCapsDeAUno) continue;
            c.tipo = tipo;
            c.original = k->set_caps;
            k->set_caps = setCapsDeAUno;
        }
    });
}

void ponerRegistro(std::function<void(Nivel, const std::string&)> f) {
    std::lock_guard<std::mutex> l(g_regMu);
    g_registro = std::move(f);
}

void registrar(Nivel n, const std::string& texto) {
    std::lock_guard<std::mutex> l(g_regMu);
    if (g_registro) {
        g_registro(n, texto);
        return;
    }
    std::fprintf(stderr, "[motor] %s%s\n", n == Nivel::Error ? "ERROR: " : n == Nivel::Aviso ? "aviso: " : "",
                 texto.c_str());
}

Reloj::Reloj(const std::string& tipo, const std::string& servidor, int puerto, int dominio) {
    if (tipo.empty() || tipo == "sistema") {
        clock_ = gst_system_clock_obtain(); // monotónico
    } else if (tipo == "ntp") {
        if (servidor.empty())
            error_ = "falta el servidor NTP";
        else
            clock_ = gst_ntp_clock_new("ba-ntp", servidor.c_str(), puerto > 0 ? puerto : 123, 0);
    } else if (tipo == "red") {
        if (servidor.empty())
            error_ = "falta el aparato que publica el reloj";
        else
            clock_ = gst_net_client_clock_new("ba-red", servidor.c_str(), puerto > 0 ? puerto : 8554, 0);
    } else if (tipo == "ptp") {
        static bool ptpIniciado = false;
        if (!ptpIniciado) ptpIniciado = gst_ptp_init(GST_PTP_CLOCK_ID_NONE, nullptr);
        if (!ptpIniciado)
            error_ = "PTP no arrancó: necesita gst-ptp-helper con permisos para los puertos 319/320";
        else
            clock_ = gst_ptp_clock_new("ba-ptp", static_cast<guint>(std::max(0, dominio)));
    } else {
        error_ = "reloj desconocido «" + tipo + "» (sistema, ntp, ptp o red)";
    }
    // NTP, PTP y el de otro aparato arrancan SIN sincronizar (con la hora de la máquina) y
    // al sincronizar SALTAN a la suya (NTP cuenta desde 1900): con la base tomada antes, todo
    // lo medido quedaba corrido ~126 años y las entradas «llegaban tarde» para siempre. Se
    // espera la sincronización (un rato: es un cambio que pide el operador) y, si no llega,
    // el del sistema.
    if (clock_ != nullptr && tipo != "sistema" && !tipo.empty() &&
        GST_OBJECT_FLAG_IS_SET(clock_, GST_CLOCK_FLAG_NEEDS_STARTUP_SYNC) && !gst_clock_is_synced(clock_) &&
        !gst_clock_wait_for_sync(clock_, kEsperaSyncNs)) {
        error_ = "el reloj " + tipo + " no se sincronizó en " + std::to_string(kEsperaSyncNs / GST_SECOND) + " s";
        gst_object_unref(clock_);
        clock_ = nullptr;
    }
    if (clock_ == nullptr) {
        if (error_.empty()) error_ = "no se pudo crear el reloj " + tipo;
        registrar(Nivel::Error, error_ + ": se usa el del sistema");
        clock_ = gst_system_clock_obtain();
    }
    base_ = gst_clock_get_time(clock_);
}

Reloj::~Reloj() {
    if (clock_ != nullptr) gst_object_unref(clock_);
}

GstClockTime Reloj::ahora() const {
    const GstClockTime t = gst_clock_get_time(clock_);
    return t > base_ ? t - base_ : 0;
}

void Reloj::adoptar(GstElement* pipeline) const {
    registrarPipeline(pipeline); // para el mapa (Mapa.hpp)
    darContextoGpu(pipeline);    // el GL del motor, el mismo para todos
    gst_pipeline_use_clock(GST_PIPELINE(pipeline), clock_);
    gst_element_set_start_time(pipeline, GST_CLOCK_TIME_NONE); // el base_time no lo cambia solo
    gst_element_set_base_time(pipeline, base_);
}

void Reloj::ponerTope(std::int64_t ms) {
    std::lock_guard<std::mutex> l(mu_);
    topeMs_ = ms < 0 ? -1 : ms;
}

std::int64_t Reloj::topeMs() const {
    std::lock_guard<std::mutex> l(mu_);
    return topeMs_ >= 0 ? topeMs_ : kTopeAutomaticoMs;
}

bool Reloj::topeAutomatico() const {
    std::lock_guard<std::mutex> l(mu_);
    return topeMs_ < 0;
}

bool Reloj::llegaTarde(std::int64_t ns) const { return ns > topeMs() * static_cast<std::int64_t>(GST_MSECOND); }

std::int64_t Reloj::anotar(const std::string& quien, std::int64_t ns) {
    {
        std::lock_guard<std::mutex> l(mu_);
        pedidas_[quien] = std::max<std::int64_t>(0, ns);
    }
    return latencia();
}

void Reloj::olvidar(const std::string& quien) {
    std::lock_guard<std::mutex> l(mu_);
    pedidas_.erase(quien);
}

std::int64_t Reloj::objetivoSinLock() const {
    const std::int64_t tope = (topeMs_ >= 0 ? topeMs_ : kTopeAutomaticoMs) * static_cast<std::int64_t>(GST_MSECOND);
    std::int64_t m = 0;
    for (const auto& [x, v] : pedidas_)
        if (v <= tope) m = std::max(m, v); // las que pasan el tope no arrastran
    return m;
}

std::int64_t Reloj::objetivo() const {
    std::lock_guard<std::mutex> l(mu_);
    return objetivoSinLock();
}

std::string Reloj::laMarca() const {
    const std::int64_t tope = topeMs() * static_cast<std::int64_t>(GST_MSECOND);
    std::lock_guard<std::mutex> l(mu_);
    std::string quien;
    std::int64_t m = -1;
    for (const auto& [x, v] : pedidas_)
        if (v <= tope && v > m) m = v, quien = x;
    return quien;
}

std::int64_t Reloj::latencia() const {
    std::lock_guard<std::mutex> l(mu_);
    return fijada_ >= 0 ? fijada_ : objetivoSinLock();
}

bool Reloj::revisarLatencia(bool puedeBajar) {
    std::lock_guard<std::mutex> l(mu_);
    const std::int64_t ahora = static_cast<std::int64_t>(g_get_monotonic_time()) * 1000;
    const std::int64_t obj = objetivoSinLock();
    if (arranque_ == 0) arranque_ = ahora;
    // Los primeros segundos (las entradas recién arrancan y miden): sigue al objetivo.
    if (fijada_ < 0 || ahora - arranque_ < 8 * static_cast<std::int64_t>(GST_SECOND)) {
        const bool cambio = fijada_ != obj;
        fijada_ = obj;
        return cambio && ahora - arranque_ >= 8 * static_cast<std::int64_t>(GST_SECOND);
    }
    if (obj > fijada_) {
        bajaDesde_ = 0;
        if (subeDesde_ == 0) subeDesde_ = ahora;
        if (ahora - subeDesde_ >= kSostenerSubidaNs) {
            fijada_ = obj;
            subeDesde_ = 0;
            return true;
        }
        return false;
    }
    subeDesde_ = 0;
    if (puedeBajar && obj < fijada_ - 20 * static_cast<std::int64_t>(GST_MSECOND)) {
        if (bajaDesde_ == 0) bajaDesde_ = ahora;
        if (ahora - bajaDesde_ >= kSostenerBajadaNs) {
            fijada_ = obj;
            bajaDesde_ = 0;
            return true;
        }
    } else {
        bajaDesde_ = 0;
    }
    return false;
}

void Reloj::saltearArranque() {
    std::lock_guard<std::mutex> l(mu_);
    arranque_ = static_cast<std::int64_t>(g_get_monotonic_time()) * 1000 - 60 * static_cast<std::int64_t>(GST_SECOND);
    if (fijada_ < 0) fijada_ = objetivoSinLock();
}

bool Reloj::bajarLatencia() {
    std::lock_guard<std::mutex> l(mu_);
    const std::int64_t obj = objetivoSinLock();
    if (fijada_ >= 0 && obj >= fijada_) return false;
    fijada_ = obj;
    bajaDesde_ = subeDesde_ = 0;
    return true;
}

bool Reloj::puedeBajarse() const {
    std::lock_guard<std::mutex> l(mu_);
    return fijada_ >= 0 && objetivoSinLock() < fijada_ - 20 * static_cast<std::int64_t>(GST_MSECOND);
}

std::map<std::string, std::int64_t> Reloj::anotadas() const {
    std::lock_guard<std::mutex> l(mu_);
    return pedidas_;
}

} // namespace ba::motor
