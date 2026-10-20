// ============================================================================
// src/motor/Composicion.cpp — ver Composicion.hpp
// ============================================================================
#include "Composicion.hpp"
#include "diag/Utf8.hpp"

#include "Mezcla.hpp" // margenMezclaNs
#include "Reloj.hpp"

#include <gst/video/video.h>

#include <algorithm>
#include <set>

#include "ajustes/Ajustes.hpp"
#include <cstdlib>
#include <atomic>
#include <cmath>

namespace ba::motor {

using json = nlohmann::json;

namespace {
std::int64_t ahoraNs() { return g_get_monotonic_time() * 1000; }

std::string nombreSeguro(const std::string& s) {
    std::string n = s;
    for (char& c : n)
        if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
    return n;
}

} // namespace

// --- Sombra -----------------------------------------------------------------------------

int SombraCapa::margen() const {
    if (alfa <= 0) return 0;
    const double m = std::max(0.0, difuminado) + std::max(0.0, distancia) + std::max(0.0, tamano) + 2;
    return (static_cast<int>(std::ceil(m)) + 7) / 8 * 8;
}

SombraCapa SombraCapa::desdeJson(const nlohmann::json& j) {
    SombraCapa s;
    if (!j.is_object()) return s;
    s.alfa = std::clamp(j.value("alfa", 0.0), 0.0, 1.0);
    if (j.contains("color") && j["color"].is_number_unsigned()) {
        s.color = j["color"].get<std::uint32_t>();
    } else if (j.contains("color") && j["color"].is_string()) {
        // "#RRGGBB" o "#AARRGGBB"
        std::string h = j["color"].get<std::string>();
        if (!h.empty() && h[0] == '#') h.erase(0, 1);
        char* fin = nullptr;
        const unsigned long v = std::strtoul(h.c_str(), &fin, 16);
        if (fin != nullptr && *fin == '\0' && (h.size() == 6 || h.size() == 8))
            s.color = static_cast<std::uint32_t>(h.size() == 6 ? (0xFF000000UL | v) : v);
    }
    s.difuminado = std::clamp(j.value("difuminado", 20.0), 0.0, 500.0);
    s.distancia = std::clamp(j.value("distancia", 10.0), 0.0, 1000.0);
    s.angulo = j.value("angulo", 45.0);
    s.tamano = std::clamp(j.value("tamano", 0.0), -500.0, 500.0);
    s.borde = std::clamp(j.value("borde", 0.0), 0.0, 1000.0);
    s.radio = std::clamp(j.value("radio", 0.0), 0.0, 2000.0);
    return s;
}

nlohmann::json SombraCapa::aJson() const {
    return {{"alfa", alfa}, {"color", color}, {"difuminado", difuminado}, {"distancia", distancia},
            {"angulo", angulo}, {"tamano", tamano}, {"borde", borde}, {"radio", radio}};
}

// --- Escena ---------------------------------------------------------------------------

void Escena::asignarClaves() {
    // La rama de cada capa se guarda por su clave: dos iguales y una pisaba a la otra.
    std::set<std::string> usadas;
    for (auto& k : capas) {
        k.clave.clear();
        std::string cl = k.entrada;
        for (int n = 2; usadas.count(cl) > 0; ++n) cl = k.entrada + "#" + std::to_string(n);
        if (cl != k.entrada) k.clave = cl;
        usadas.insert(cl);
    }
}

Escena Escena::desdeJson(const nlohmann::json& j) {
    Escena e;
    e.nombre = j.value("nombre", std::string());
    for (const auto& c : j.value("capas", json::array())) {
        if (!c.is_object()) continue;
        Capa k;
        k.entrada = c.value("entrada", std::string());
        k.productor = c.value("productor", std::string());
        k.x = c.value("x", 0);
        k.y = c.value("y", 0);
        k.ancho = c.value("ancho", 0);
        k.alto = c.value("alto", 0);
        k.alfa = c.value("alfa", 1.0);
        k.orden = c.value("orden", 0);
        const json r = c.value("recorte", json::object());
        k.recIzq = r.value("izq", 0);
        k.recArr = r.value("arr", 0);
        k.recDer = r.value("der", 0);
        k.recAba = r.value("aba", 0);
        k.visible = c.value("visible", true);
        k.contener = c.value("contener", false);
        k.sombra = SombraCapa::desdeJson(c.value("sombra", json::object()));
        if (!k.entrada.empty()) e.capas.push_back(k);
    }
    e.asignarClaves();
    const json l = j.value("lienzo", json::object());
    if (l.is_object()) {
        e.lienzoAncho = l.value("ancho", 0);
        e.lienzoAlto = l.value("alto", 0);
    }
    if (j.contains("filtros") && j["filtros"].is_array()) e.filtros = j["filtros"];
    return e;
}

nlohmann::json Escena::aJson() const {
    nlohmann::json capas = nlohmann::json::array();
    for (const auto& c : this->capas) {
        capas.push_back({{"entrada", c.entrada},
                         {"x", c.x},
                         {"y", c.y},
                         {"ancho", c.ancho},
                         {"alto", c.alto},
                         {"alfa", c.alfa},
                         {"orden", c.orden},
                         {"recorte", {{"izq", c.recIzq}, {"arr", c.recArr}, {"der", c.recDer}, {"aba", c.recAba}}},
                         {"visible", c.visible},
                         {"contener", c.contener}});
        if (c.sombra.hace()) capas.back()["sombra"] = c.sombra.aJson();
        if (!c.productor.empty()) capas.back()["productor"] = c.productor; // desdeJson lo lee: ida y vuelta igual
    }
    nlohmann::json out{{"nombre", nombre}, {"capas", capas}};
    if (lienzoAncho > 0 && lienzoAlto > 0) out["lienzo"] = {{"ancho", lienzoAncho}, {"alto", lienzoAlto}};
    if (!filtros.empty()) out["filtros"] = filtros;
    return out;
}

// --- Composición ---------------------------------------------------------------------

Composicion::Composicion(Reloj& reloj, const Lienzo& lienzo, std::string nombre, Opciones op)
    : reloj_(reloj), lienzo_(lienzo), nombre_(std::move(nombre)), gpu_((op.gpu && hayFabrica("glvideomixer")) || gpuVideo()),
      gl_(gpu_ && gpu() != Gpu::D3D12) {
    const std::string tam = "width=" + std::to_string(lienzo_.ancho) + ",height=" + std::to_string(lienzo_.alto) +
                            ",framerate=" + std::to_string(lienzo_.fpsN) + "/" + std::to_string(lienzo_.fpsD) +
                            ",pixel-aspect-ratio=1/1";
    // La grilla de cuadros de los mezcladores, alineada a la duración de un cuadro sobre la hora
    // del estudio (la misma para el de la escena y el del DSK). Con «first» cada uno la armaba
    // desde su primer buffer: la del DSK quedaba corrida un poco y, para cada cuadro, seguía
    // usando el anterior (que «todavía no terminó»): todo lo que salía llevaba la hora del
    // cuadro siguiente (33 ms) y el video quedaba un cuadro atrás del audio en las salidas.
    const GstClockTime cuadro = gst_util_uint64_scale(GST_SECOND, static_cast<guint64>(std::max(1, lienzo_.fpsD)),
                                                      static_cast<guint64>(std::max(1, lienzo_.fpsN)));
    // Un poco ADELANTE (100 ms, al próximo cuadro de la grilla): la hora de ahora ya quedó atrás
    // cuando el pipeline termina de arrancar (con la máquina cargada, bastante), y un mezclador
    // que empieza atrasado saca cuadros vencidos para ponerse al día que lo de después tira. La
    // grilla sigue en múltiplos del cuadro desde el cero del estudio: todas quedan alineadas.
    const GstClockTime ahora = reloj_.ahora();
    const GstClockTime inicio = GST_CLOCK_TIME_IS_VALID(ahora) && cuadro > 0 ? ((ahora + 100 * GST_MSECOND) / cuadro + 1) * cuadro : 0;
    const std::string comun = "force-live=true start-time-selection=set start-time=" + std::to_string(inicio) + " background=black ";
    // En GPU todo queda en la GPU (GLMemory o D3D12Memory): lo que sale es una
    // textura (el que consume la baja si hace falta, chica o en NV12: docs/desarrollo/MAPA-GPU-CPU.md).
    const std::string salida = gl_    ? "video/x-raw(memory:GLMemory),format=RGBA," + tam
                               : gpu_ ? "video/x-raw(memory:D3D12Memory),format=RGBA," + tam
                                      : capsColorDelLienzo(lienzo_) + "," + tam;
    const std::string mezclador = gl_ ? "glvideomixer" : gpu_ ? "d3d12compositor" : "compositor";
    const std::string desc = gpu_ ? mezclador + " name=mx " + comun + "! " + salida
                                  : "compositor name=mx " + comun + "! video/x-raw," + tam + " ! videoconvert ! " + salida;
    std::string completo;
    const std::vector<Filtro> filtros = op.dsk ? std::vector<Filtro>{} : leerFiltros(op.filtros);
    if (op.dsk) {
        // Dos etapas: la escena (limpia) por un tee, y el DSK encima en otro mezclador.
        completo = desc + " ! tee name=t t. ! queue ! intersink name=limpio producer-name=" + entreComillas(productorLimpio()) +
                   " sync=false t. ! queue ! dsk.sink_0 " + mezclador + " name=dsk " + comun + "! " +
                   (gpu_ ? salida : "video/x-raw," + tam + " ! videoconvert ! " + salida) +
                   " ! intersink name=sale producer-name=" + entreComillas(productor()) + " sync=false";
    } else {
        // Los filtros de después del mezclador (la Vista 360 de la Cámara 360): en la
        // GPU, su par (ba360gl, ba360d3d12) sobre lo que sale del mezclador, sin bajarlo.
        firmaFiltros_ = firmaDe(filtros, {}, false);
        const std::string cadena = cadenaDe(filtros, "videoconvert", "ba_c_", avisosFiltros_, gpu_);
        for (const auto& a : avisosFiltros_) registrar(Nivel::Aviso, "composición " + nombre_ + ": " + a);
        completo = desc + " ! " + cadena + "intersink name=sale producer-name=" + entreComillas(productor()) + " sync=false";
    }
    GError* e = nullptr;
    pipeline_ = gst_parse_launch(completo.c_str(), &e);
    if (e != nullptr) {
        error_ = ba::diag::utf8Valido(e->message);
        g_error_free(e);
        if (pipeline_ != nullptr) gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        registrar(Nivel::Error, "composición " + nombre_ + ": " + error_);
        return;
    }
    // Las propiedades de los filtros de después del mezclador (la cadena solo los nombra).
    if (!filtros.empty()) ponerEnVivo(pipeline_, filtros, "ba_c_");
    mezclador_ = gst_bin_get_by_name(GST_BIN(pipeline_), "mx");
    dskMx_ = gst_bin_get_by_name(GST_BIN(pipeline_), "dsk");
    // BA_MEDIR_VISTAS=1: cuántos cuadros salen del mezclador (y del DSK) y su
    // hueco más largo, en horas del pipeline y del reloj de pared, cada 2 s.
    if (const char* m = std::getenv("BA_MEDIR_VISTAS"); m != nullptr && std::string(m) == "1")
        for (GstElement* el : {mezclador_, dskMx_}) {
            if (el == nullptr) continue;
            struct Medida {
                std::string nombre;
                std::int64_t desde = 0, ultimo = 0, hueco = 0, ultimoPts = -1, huecoPts = 0;
                int n = 0;
            };
            auto* md = new Medida{nombre_ + (el == dskMx_ ? ":dsk" : ":mx")};
            GstPad* pad = gst_element_get_static_pad(el, "src");
            gst_pad_add_probe(
                pad, GST_PAD_PROBE_TYPE_BUFFER,
                [](GstPad*, GstPadProbeInfo* info, gpointer d) -> GstPadProbeReturn {
                    auto* md = static_cast<Medida*>(d);
                    GstBuffer* b = GST_PAD_PROBE_INFO_BUFFER(info);
                    const std::int64_t ahora = g_get_monotonic_time();
                    if (md->desde == 0) md->desde = ahora;
                    if (md->ultimo) md->hueco = std::max(md->hueco, ahora - md->ultimo);
                    md->ultimo = ahora;
                    if (GST_BUFFER_PTS_IS_VALID(b)) {
                        const auto pts = static_cast<std::int64_t>(GST_BUFFER_PTS(b));
                        if (md->ultimoPts >= 0) md->huecoPts = std::max(md->huecoPts, pts - md->ultimoPts);
                        md->ultimoPts = pts;
                    }
                    ++md->n;
                    if (ahora - md->desde >= 2'000'000) {
                        registrar(Nivel::Info, "mezcla " + md->nombre + ": " +
                                                   std::to_string(static_cast<int>(md->n * 1e6 / (ahora - md->desde) + 0.5)) +
                                                   " cuadros/s, hueco " + std::to_string(md->hueco / 1000) + " ms (pts " +
                                                   std::to_string(md->huecoPts / 1000000) + " ms)");
                        md->desde = ahora;
                        md->n = 0;
                        md->hueco = md->huecoPts = 0;
                    }
                    return GST_PAD_PROBE_OK;
                },
                md, [](gpointer d) { delete static_cast<Medida*>(d); });
            gst_object_unref(pad);
        }
    reloj_.adoptar(pipeline_);
    aplicarLatencia();
    // La edad de lo que sale (lo que pasa de verdad, no la latencia fijada).
    if (GstElement* sale = gst_bin_get_by_name(GST_BIN(pipeline_), "sale")) {
        edadSalida_.ponerEnEntrada(sale, reloj_);
        gst_object_unref(sale);
    }
    if (gst_element_set_state(pipeline_, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        error_ = "el compositor no arrancó";
        registrar(Nivel::Error, "composición " + nombre_ + ": " + error_);
    }
}

Composicion::~Composicion() {
    std::lock_guard<std::mutex> l(mu_);
    if (pipeline_ == nullptr) return;
    edadSalida_.sacar();
    gst_element_set_state(pipeline_, GST_STATE_NULL);
    for (auto* m : {&ramas_, &dsk_}) {
        for (auto& [k, r] : *m) {
            if (r->pad != nullptr) {
                gst_element_release_request_pad(r->mx, r->pad);
                gst_object_unref(r->pad);
            }
            delete r;
        }
        m->clear();
    }
    if (dskMx_ != nullptr) gst_object_unref(dskMx_);
    gst_object_unref(mezclador_);
    gst_object_unref(pipeline_);
}

void Composicion::aplicarLatencia() {
    // La latencia del estudio + el margen del VIDEO (5 ms de fábrica, video.margenComposicionMs):
    // el temblor de cada entrada ya va en lo que pide (Entrada: p90 + lo que tiembla), así que no
    // hace falta el colchón de 30 ms del audio. La estabilidad la da el Reloj (cambia poco y a
    // propósito): acá se la sigue siempre (antes, una baja de menos de 60 ms no se aplicaba).
    const std::int64_t l = reloj_.latencia() + ajustes::msANs("video.margenComposicionMs", 5);
    if (l == latenciaNs_) return;
    latenciaNs_ = l;
    // El plazo de cada capa: lo que espera el mezclador + un cuadro (lo que llega más viejo, tarde).
    const std::int64_t plazo = l + static_cast<std::int64_t>(gst_util_uint64_scale(GST_SECOND, static_cast<guint64>(std::max(1, lienzo_.fpsD)),
                                                                                       static_cast<guint64>(std::max(1, lienzo_.fpsN))));
    for (auto& [k, r] : ramas_) r->llega.ponerPlazo(plazo);
    // Todo en otro hilo (ver recalcularLatenciaAparte): esto corre en el bucle del motor.
    recalcularLatenciaAparte(pipeline_, [mx = refCompartida(mezclador_), dsk = refCompartida(dskMx_), l]() {
        g_object_set(mx.get(), "min-upstream-latency", static_cast<guint64>(l), nullptr);
        if (dsk) g_object_set(dsk.get(), "min-upstream-latency", static_cast<guint64>(l), nullptr);
    });
}

GstPadProbeReturn Composicion::alPrimerCuadro(GstPad*, GstPadProbeInfo*, gpointer d) {
    auto* r = static_cast<Rama*>(d);
    r->listo = true;
    return GST_PAD_PROBE_REMOVE;
}

Composicion::Rama* Composicion::crearRama(const Capa& c, int gen, GstElement* mx) {
    if (mx == nullptr) mx = mezclador_;
    const std::int64_t t0 = g_get_monotonic_time(); // si tarda, cuánto en crear y cuánto en arrancar
    // Único en el pipeline: limpio, «Cam 1» y «Cam-1» (o «Cámara» y «Cámera») quedaban iguales.
    const std::string n = nombreSeguro(std::to_string(gen) + "_" + c.claveCapa()) + "_" + std::to_string(++serieRamas_);
    auto* r = new Rama();
    r->entrada = c.entrada;
    r->clave = c.claveCapa();
    r->productor = c.productor.empty() ? c.entrada + ":video" : c.productor;
    r->generacion = gen;
    r->src = gst_element_factory_make("intersrc", ("src_" + n).c_str());
    r->recorte = gst_element_factory_make(gl_ ? "gltransformation" : gpu_ ? "d3d12convert" : "videocrop", ("rec_" + n).c_str());
    if (gpu_ && !gl_) {
        // D3D12: una sonda marca la ventana en cada cuadro (meta de recorte, sin cambiar
        // el tamaño: nada se renegocia, ni en la cortina) y d3d12convert la aplica en la GPU.
        r->subir = gst_element_factory_make("d3d12upload", ("d3d_" + n).c_str());
    } else if (gpu_) {
        r->subir = gst_element_factory_make("glupload", ("gl_" + n).c_str());
        r->conv = gst_element_factory_make("glcolorconvert", ("glc_" + n).c_str());
        // Sin un elemento que dibuje detrás, gltransformation no dibuja: le pasa
        // al mezclador una transformación afín que glvideomixer aplica SIN
        // recortar a la caja de la capa (lo ampliado se sale). glshader, con su
        // shader de fábrica, copia la textura: una pasada barata en la GPU.
        r->fijar = gst_element_factory_make("glshader", ("gls_" + n).c_str());
    }
    if (r->src == nullptr || r->recorte == nullptr || (gpu_ && (r->subir == nullptr || (gl_ && (r->conv == nullptr || r->fijar == nullptr))))) {
        for (GstElement* e : {r->src, r->recorte, r->subir, r->conv, r->fijar})
            if (e != nullptr) gst_object_unref(e);
        delete r;
        error_ = gl_    ? "falta intersrc, queue, glupload, glcolorconvert o gltransformation"
                 : gpu_ ? "falta intersrc, queue, d3d12upload o d3d12convert"
                        : "falta intersrc, queue o videocrop";
        registrar(Nivel::Error, "composición " + nombre_ + ": " + error_);
        return nullptr;
    }
    if (c.sombra.hace()) {
        // La sombra (y el borde difuso): después del recorte, con su par de la GPU. En GL
        // dibuja, así que también obliga a gltransformation a dibujar: el glshader sobra.
        const char* fab = gl_ ? "basombragl" : gpu_ ? "basombrad3d12" : "basombra";
        r->sombra = gst_element_factory_make(fab, ("som_" + n).c_str());
        if (r->sombra != nullptr && !gpu_) r->aRgba = gst_element_factory_make("videoconvert", ("rgba_" + n).c_str());
        if (r->sombra == nullptr || (!gpu_ && r->aRgba == nullptr)) {
            registrar(Nivel::Aviso, "composición " + nombre_ + ": falta " + fab + " (la capa " + c.entrada + " va sin sombra)");
            for (GstElement** e : {&r->sombra, &r->aRgba})
                if (*e != nullptr) gst_object_unref(*e), *e = nullptr;
        } else if (r->fijar != nullptr) {
            gst_object_unref(r->fijar);
            r->fijar = nullptr;
        }
    }
    const std::int64_t tCreados = g_get_monotonic_time();
    if (gl_) g_object_set(r->recorte, "ortho", TRUE, nullptr);
    g_object_set(r->src, "producer-name", r->productor.c_str(), nullptr);
    // Sin demora propia: si se atrasa, se tira lo viejo (el compositor espera la
    // latencia). Lo hace la cola del intersrc (descarta lo viejo, como una queue
    // leaky): una queue más atrás era un hilo más por capa y no cambiaba nada.
    g_object_set(r->src, "max-buffers", static_cast<guint64>(3), "max-time", static_cast<guint64>(0), nullptr);
    if (GstPad* p = gst_element_get_static_pad(r->src, "src")) {
        r->llega.ponerPlazo(latenciaNs_ > 0 ? latenciaNs_ + static_cast<std::int64_t>(gst_util_uint64_scale(
                                                                      GST_SECOND, static_cast<guint64>(std::max(1, lienzo_.fpsD)),
                                                                      static_cast<guint64>(std::max(1, lienzo_.fpsN))))
                                              : 0);
        r->llega.poner(p, reloj_);
        gst_object_unref(p);
    }
    gst_bin_add_many(GST_BIN(pipeline_), r->src, r->recorte, nullptr);
    if (gpu_ && !gl_) {
        // intersrc → subir a D3D12 (si ya está, pasa) → (marca) → d3d12convert aplica el recorte.
        gst_bin_add(GST_BIN(pipeline_), r->subir);
        enlazarSinConsultar({r->src, r->subir, r->recorte});
        if (r->sombra != nullptr) {
            gst_bin_add(GST_BIN(pipeline_), r->sombra);
            enlazarSinConsultar({r->recorte, r->sombra});
        }
        if (GstPad* p = gst_element_get_static_pad(r->recorte, "sink")) {
            gst_pad_add_probe(p, static_cast<GstPadProbeType>(GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM),
                              &Composicion::marcarRecorte, r, nullptr);
            gst_object_unref(p);
        }
    } else if (gpu_) {
        // intersrc → subir a GL (si ya es GL, pasa) → RGBA → recorte en GL.
        GstElement* ultimo = r->sombra != nullptr ? r->sombra : r->fijar;
        gst_bin_add_many(GST_BIN(pipeline_), r->subir, r->conv, ultimo, nullptr);
        enlazarSinConsultar({r->src, r->subir, r->conv, r->recorte, ultimo});
        if (GstPad* p = gst_element_get_static_pad(r->recorte, "sink")) {
            gst_pad_add_probe(p, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, &Composicion::avisarTamano, r, nullptr);
            gst_object_unref(p);
        }
    } else if (r->sombra != nullptr) {
        // CPU: la sombra necesita alfa (RGBA o BGRA); el mezclador lo toma así.
        gst_bin_add_many(GST_BIN(pipeline_), r->aRgba, r->sombra, nullptr);
        enlazarSinConsultar({r->src, r->recorte, r->aRgba, r->sombra});
    } else {
        enlazarSinConsultar({r->src, r->recorte});
    }
    const std::int64_t tEnlazados = g_get_monotonic_time();
    r->mx = mx;
    r->pad = gst_element_request_pad_simple(mx, "sink_%u");
    GstPad* sale = gst_element_get_static_pad(r->sombra != nullptr ? r->sombra : r->fijar != nullptr ? r->fijar : r->recorte, "src");
    gst_pad_link_full(sale, r->pad, GST_PAD_LINK_CHECK_NOTHING); // sin consultar caps (ver enlazarSinConsultar)
    gst_pad_add_probe(sale, GST_PAD_PROBE_TYPE_BUFFER, &Composicion::alPrimerCuadro, r, nullptr);
    gst_object_unref(sale);
    const std::int64_t t1 = g_get_monotonic_time();
    for (GstElement* e : {r->sombra, r->aRgba, r->fijar, r->recorte, r->conv, r->subir, r->src})
        if (e != nullptr) gst_element_sync_state_with_parent(e);
    const std::int64_t t2 = g_get_monotonic_time();
    // Arma con el candado del motor (mostrar lo llama el bucle): si una capa tarda, el registro
    // dice si fue crear y enganchar los elementos o ponerlos en marcha (en D3D12, la primera
    // de cada clase compila sus shaders).
    if (t2 - t0 > 20'000)
        registrar(Nivel::Info, "composición " + nombre_ + ": la capa «" + c.entrada + "» se armó en " + std::to_string((t2 - t0) / 1000) +
                                   " ms (crear " + std::to_string((tCreados - t0) / 1000) + ", agregar y enlazar " +
                                   std::to_string((tEnlazados - tCreados) / 1000) + ", el pad del mezclador " +
                                   std::to_string((t1 - tEnlazados) / 1000) + ", arrancar " + std::to_string((t2 - t1) / 1000) + ")");
    if (mx == mezclador_) ramas_[std::to_string(gen) + "/" + c.claveCapa()] = r;
    return r;
}

void Composicion::ponerSombra(Rama& r, const SombraCapa& s, int margen, int cajaAncho, int cajaAlto, bool contener) {
    if (r.sombra == nullptr) return;
    g_object_set(r.sombra, "caja-ancho", cajaAncho, "caja-alto", cajaAlto, "contener", contener ? TRUE : FALSE, "margen", margen,
                 "alfa", s.alfa, "color", static_cast<guint>(s.color), "difuminado", s.difuminado, "distancia", s.distancia,
                 "angulo", s.angulo, "tamano", s.tamano, "borde", s.borde, "radio", s.radio, nullptr);
}

void Composicion::ponerCapa(Rama& r, const Capa& c, double alfa) {
    const int w = c.ancho > 0 ? c.ancho : lienzo_.ancho;
    const int h = c.alto > 0 ? c.alto : lienzo_.alto;
    // Con sombra, la caja crece `m` de cada lado: la imagen queda donde estaba y la sombra, afuera.
    const int m = r.sombra != nullptr ? c.sombra.margen() : 0;
    r.margen = m;
    ponerSombra(r, c.sombra, m, w, h, c.contener);
    // Las de la escena que entra van arriba de las de la que sale (así la mezcla pasa
    // por encima): 5000 + orden; al cambiar de escena las viejas bajan a su orden (ver
    // bajarViejas). glvideomixer no acepta un zorder de más de 10000.
    const guint zorder = static_cast<guint>((r.generacion == generacion_ && r.generacion > 0 ? 5000 : 0) + std::clamp(c.orden, 0, 999));
    g_object_set(r.pad, "xpos", c.x - m, "ypos", c.y - m, "width", w + 2 * m, "height", h + 2 * m, "alpha",
                 c.visible ? alfa : 0.0, "zorder", zorder, nullptr);
    gst_util_set_object_arg(G_OBJECT(r.pad), "sizing-policy", c.contener ? "keep-aspect-ratio" : "none");
    if (gpu_)
        recorteGl(r, c, 0, w, h); // sin tamaño todavía: se pone cuando llegue (avisarTamano)
    else
        g_object_set(r.recorte, "left", std::max(0, c.recIzq), "top", std::max(0, c.recArr), "right", std::max(0, c.recDer),
                     "bottom", std::max(0, c.recAba), nullptr);
    r.alfaHacia = c.visible ? c.alfa : 0.0;
    r.orden = c.orden;
    r.capa = c;
}

bool Composicion::recorteGl(Rama& r, const Capa& c, int extraDer, int ancho, int alto) {
    // El tamaño de la fuente (lo que llega al recorte).
    int sw = 0, sh = 0;
    if (GstPad* p = gst_element_get_static_pad(r.recorte, "sink")) {
        if (GstCaps* k = gst_pad_get_current_caps(p)) {
            gst_structure_get_int(gst_caps_get_structure(k, 0), "width", &sw);
            gst_structure_get_int(gst_caps_get_structure(k, 0), "height", &sh);
            gst_caps_unref(k);
        }
        gst_object_unref(p);
    }
    const int izq = std::max(0, c.recIzq), arr = std::max(0, c.recArr), der = std::max(0, c.recDer) + std::max(0, extraDer),
              aba = std::max(0, c.recAba);
    if (!gl_) {
        // D3D12: la sonda lo marca en el próximo cuadro (en píxeles de la fuente).
        r.recIzq = izq;
        r.recArr = arr;
        r.recDer = der;
        r.recAba = aba;
    }
    if (sw <= 0 || sh <= 0 || izq + der + arr + aba == 0) {
        if (gl_) g_object_set(r.recorte, "scale-x", 1.0f, "scale-y", 1.0f, "translation-x", 0.0f, "translation-y", 0.0f, nullptr);
        // Con recorte y sin tamaño todavía: no se pudo (en D3D12 la sonda lo marca igual).
        return !gl_ || izq + der + arr + aba == 0;
    }
    // La región visible, en fracciones de la fuente; gltransformation la estira a todo el cuadro.
    const double u0 = std::clamp(static_cast<double>(izq) / sw, 0.0, 0.95), u1 = std::clamp(1.0 - static_cast<double>(der) / sw, u0 + 0.01, 1.0);
    const double v0 = std::clamp(static_cast<double>(arr) / sh, 0.0, 0.95), v1 = std::clamp(1.0 - static_cast<double>(aba) / sh, v0 + 0.01, 1.0);
    const double sx = 1.0 / (u1 - u0), sy = 1.0 / (v1 - v0);
    if (gl_)
        g_object_set(r.recorte, "scale-x", static_cast<gfloat>(sx), "scale-y", static_cast<gfloat>(sy), "translation-x",
                 static_cast<gfloat>((0.5 - (u0 + u1) / 2.0) * sx), "translation-y",
                 static_cast<gfloat>((0.5 - (v0 + v1) / 2.0) * sy), nullptr);
    // Con «contener»: la caja con la forma de lo recortado (el mezclador vería la de la fuente entera).
    if (c.contener) {
        const double aspecto = ((u1 - u0) * sw) / ((v1 - v0) * sh);
        int w = ancho, h = static_cast<int>(std::lround(ancho / aspecto));
        if (h > alto) {
            h = alto;
            w = static_cast<int>(std::lround(alto * aspecto));
        }
        gst_util_set_object_arg(G_OBJECT(r.pad), "sizing-policy", "none");
        // La sombra, en la caja que queda (la imagen llega estirada a todo el cuadro).
        const int m = r.margen;
        ponerSombra(r, c.sombra, m, w, h, false);
        g_object_set(r.pad, "xpos", c.x + (ancho - w) / 2 - m, "ypos", c.y + (alto - h) / 2 - m, "width", w + 2 * m, "height",
                     h + 2 * m, nullptr);
    }
    return true;
}

GstPadProbeReturn Composicion::avisarTamano(GstPad*, GstPadProbeInfo* info, gpointer d) {
    if (GST_EVENT_TYPE(GST_PAD_PROBE_INFO_EVENT(info)) == GST_EVENT_CAPS) static_cast<Rama*>(d)->tamanoNuevo = true;
    return GST_PAD_PROBE_OK;
}

GstPadProbeReturn Composicion::marcarRecorte(GstPad*, GstPadProbeInfo* info, gpointer d) {
    auto* r = static_cast<Rama*>(d);
    if ((info->type & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) != 0) {
        GstEvent* e = GST_PAD_PROBE_INFO_EVENT(info);
        if (GST_EVENT_TYPE(e) == GST_EVENT_CAPS) {
            GstCaps* c = nullptr;
            gst_event_parse_caps(e, &c);
            gint w = 0, h = 0;
            if (c != nullptr && gst_caps_get_size(c) > 0) {
                gst_structure_get_int(gst_caps_get_structure(c, 0), "width", &w);
                gst_structure_get_int(gst_caps_get_structure(c, 0), "height", &h);
            }
            r->fuenteW = w;
            r->fuenteH = h;
        }
        return GST_PAD_PROBE_OK;
    }
    const int izq = r->recIzq, arr = r->recArr, der = r->recDer, aba = r->recAba, W = r->fuenteW, H = r->fuenteH;
    if (izq + arr + der + aba == 0 || W <= 0 || H <= 0) return GST_PAD_PROBE_OK;
    // Sin copiar la imagen: el buffer escribible comparte la memoria (solo cambia la meta).
    GstBuffer* b = gst_buffer_make_writable(GST_PAD_PROBE_INFO_BUFFER(info));
    GST_PAD_PROBE_INFO_DATA(info) = b;
    GstVideoCropMeta* m = gst_buffer_get_video_crop_meta(b);
    if (m == nullptr) m = gst_buffer_add_video_crop_meta(b);
    m->x = static_cast<guint>(std::clamp(izq, 0, W - 2));
    m->y = static_cast<guint>(std::clamp(arr, 0, H - 2));
    m->width = static_cast<guint>(std::max(2, W - izq - der));
    m->height = static_cast<guint>(std::max(2, H - arr - aba));
    return GST_PAD_PROBE_OK;
}

int Composicion::anchoDeOrigen(const Rama& r) {
    int w = 0;
    if (GstPad* p = gst_element_get_static_pad(r.recorte, "sink")) {
        if (GstCaps* c = gst_pad_get_current_caps(p)) {
            gst_structure_get_int(gst_caps_get_structure(c, 0), "width", &w);
            gst_caps_unref(c);
        }
        gst_object_unref(p);
    }
    return w;
}

void Composicion::animar(double t) {
    const int W = lienzo_.ancho;
    for (auto& [k, r] : ramas_) {
        const bool nueva = r->generacion == generacion_;
        const Capa& c = r->capa;
        const int w = c.ancho > 0 ? c.ancho : lienzo_.ancho;
        if (transicion_ == "mezcla") {
            if (nueva) g_object_set(r->pad, "alpha", r->alfaHacia * t, nullptr);
        } else if (transicion_ == "empuje") {
            const int dx = static_cast<int>(std::lround((nueva ? (1.0 - t) : -t) * W));
            g_object_set(r->pad, "xpos", c.x - r->margen + dx, nullptr);
        } else if (transicion_ == "cortina" && nueva && (c.contener || r->sombra != nullptr)) {
            // Una capa que entra sin deformarse (o con sombra) no se puede recortar a medida: se funde.
            g_object_set(r->pad, "alpha", r->alfaHacia * t, nullptr);
        } else if (transicion_ == "cortina" && nueva) {
            const double borde = t * W;
            const int vis = static_cast<int>(std::clamp(borde - c.x, 0.0, static_cast<double>(w)));
            const int origen = anchoDeOrigen(*r);
            if (vis <= 1 || origen <= 0) {
                g_object_set(r->pad, "alpha", 0.0, nullptr);
                continue;
            }
            const int util = std::max(1, origen - c.recIzq - c.recDer);
            const int sacar = static_cast<int>(std::lround(util * (1.0 - static_cast<double>(vis) / w)));
            if (gpu_)
                recorteGl(*r, c, sacar, w, c.alto > 0 ? c.alto : lienzo_.alto);
            else
                g_object_set(r->recorte, "right", std::max(0, c.recDer) + sacar, nullptr);
            g_object_set(r->pad, "width", vis, "alpha", r->alfaHacia, nullptr);
        }
    }
}

void Composicion::soltar(Rama& r) {
    for (GstElement* e : {r.src, r.recorte, r.subir, r.conv, r.fijar, r.aRgba, r.sombra})
        if (e != nullptr) gst_element_set_state(e, GST_STATE_NULL);
    if (r.pad != nullptr) {
        gst_element_release_request_pad(r.mx != nullptr ? r.mx : mezclador_, r.pad);
        gst_object_unref(r.pad);
        r.pad = nullptr;
    }
    gst_bin_remove_many(GST_BIN(pipeline_), r.src, r.recorte, nullptr);
    if (r.subir != nullptr) gst_bin_remove(GST_BIN(pipeline_), r.subir);
    if (r.conv != nullptr) gst_bin_remove(GST_BIN(pipeline_), r.conv);
    if (r.fijar != nullptr) gst_bin_remove(GST_BIN(pipeline_), r.fijar);
    if (r.aRgba != nullptr) gst_bin_remove(GST_BIN(pipeline_), r.aRgba);
    if (r.sombra != nullptr) gst_bin_remove(GST_BIN(pipeline_), r.sombra);
}

void Composicion::bajarViejas() {
    for (auto& [k, r] : ramas_)
        if (r->pad != nullptr) g_object_set(r->pad, "zorder", static_cast<guint>(std::clamp(r->orden, 0, 999)), nullptr);
}

void Composicion::sacarGeneracion(int gen) {
    for (auto it = ramas_.begin(); it != ramas_.end();) {
        if (it->second->generacion == gen) {
            soltar(*it->second);
            delete it->second;
            it = ramas_.erase(it);
        } else {
            ++it;
        }
    }
}

std::set<std::string> Composicion::entradasEnUso() const {
    std::lock_guard<std::mutex> l(mu_);
    std::set<std::string> out;
    for (const auto& [k, r] : ramas_) out.insert(r->entrada);
    for (const auto& [k, r] : dsk_) out.insert(r->entrada);
    return out;
}

std::set<std::string> Composicion::productoresEnUso() const {
    std::lock_guard<std::mutex> l(mu_);
    std::set<std::string> out;
    for (const auto& [k, r] : ramas_) out.insert(r->productor);
    for (const auto& [k, r] : dsk_) out.insert(r->productor);
    return out;
}

void Composicion::mostrar(const Escena& e, const std::string& transicion, int ms) {
    std::lock_guard<std::mutex> l(mu_);
    if (pipeline_ == nullptr) return;
    // La misma escena (y sin transición en curso): se actualiza en el lugar.
    if (e.nombre == escena_.nombre && genVieja_ < 0 && generacion_ > 0) {
        std::map<std::string, const Capa*> nuevas;
        for (const auto& c : e.capas) nuevas[c.claveCapa()] = &c;
        for (auto it = ramas_.begin(); it != ramas_.end();) {
            auto n = nuevas.find(it->second->clave);
            const std::string prodNuevo =
                n == nuevas.end() ? std::string() : (n->second->productor.empty() ? n->second->entrada + ":video" : n->second->productor);
            // Se fue, ahora lee otra cosa, o le hace falta la sombra y su rama no la tiene
            // (se rearma una vez; después la rama la lleva aunque se apague).
            const bool sinSombra = n != nuevas.end() && n->second->sombra.hace() && it->second->sombra == nullptr;
            if (n == nuevas.end() || prodNuevo != it->second->productor || sinSombra) {
                soltar(*it->second);
                delete it->second;
                it = ramas_.erase(it);
                continue;
            }
            ponerCapa(*it->second, *n->second, n->second->alfa);
            nuevas.erase(n);
            ++it;
        }
        for (const auto& [ent, c] : nuevas)
            if (Rama* r = crearRama(*c, generacion_)) ponerCapa(*r, *c, c->alfa);
        escena_ = e;
        return;
    }
    // Una transición que no terminó: lo viejo se va ya.
    if (genVieja_ >= 0) {
        sacarGeneracion(genVieja_);
        genVieja_ = -1;
    }
    const int vieja = generacion_;
    bajarViejas();
    ++generacion_;
    const bool animada = (transicion == "mezcla" || transicion == "empuje" || transicion == "cortina") && ms > 0 && vieja > 0;
    for (const auto& c : e.capas)
        if (Rama* r = crearRama(c, generacion_)) ponerCapa(*r, c, animada && transicion != "empuje" ? 0.0 : c.alfa);
    escena_ = e;
    if (vieja > 0) {
        genVieja_ = vieja;
        transicion_ = animada ? transicion : "corte";
        transDesdeNs_ = ahoraNs();
        transNs_ = animada ? static_cast<std::int64_t>(ms) * 1000000 : ajustes::msANs("video.esperaCorteMs", 700);
        if (animada) animar(0.0);
    }
}

bool Composicion::manual(const Escena& e, double t, bool soltar) {
    std::lock_guard<std::mutex> l(mu_);
    if (pipeline_ == nullptr) return false;
    t = std::clamp(t, 0.0, 1.0);
    if (!manual_ || escena_.nombre != e.nombre) {
        // Arranca: lo que esté en transición termina ya, y la nueva entra con alfa 0.
        if (genVieja_ >= 0) {
            sacarGeneracion(genVieja_);
            genVieja_ = -1;
        }
        escenaAntes_ = escena_;
        const int vieja = generacion_;
        bajarViejas();
        ++generacion_;
        for (const auto& c : e.capas)
            if (Rama* r = crearRama(c, generacion_)) ponerCapa(*r, c, 0.0);
        escena_ = e;
        genVieja_ = vieja > 0 ? vieja : -1;
        transicion_ = "mezcla";
        manual_ = true;
    }
    manualT_ = t;
    animar(t);
    if (t < 1.0 && !soltar) return false;
    manual_ = false;
    if (t >= 0.5) {
        for (auto& [k, r] : ramas_)
            if (r->generacion == generacion_) ponerCapa(*r, r->capa, r->alfaHacia);
        if (genVieja_ >= 0) sacarGeneracion(genVieja_);
        genVieja_ = -1;
        return true;
    }
    // Antes de la mitad: vuelve lo de antes (se saca lo nuevo).
    sacarGeneracion(generacion_);
    if (genVieja_ >= 0) generacion_ = genVieja_;
    genVieja_ = -1;
    escena_ = escenaAntes_;
    return false;
}

std::string Composicion::manualHacia() const {
    std::lock_guard<std::mutex> l(mu_);
    return manual_ ? escena_.nombre : std::string();
}

bool Composicion::enTransicion() const {
    std::lock_guard<std::mutex> l(mu_);
    return genVieja_ >= 0;
}

bool Composicion::ponerDsk(const std::vector<Capa>& capas, int ms) {
    std::lock_guard<std::mutex> l(mu_);
    if (dskMx_ == nullptr) return false;
    const std::int64_t ahora = ahoraNs(), dur = static_cast<std::int64_t>(std::max(0, ms)) * 1000000;
    std::map<std::string, const Capa*> nuevas;
    for (const auto& c : capas) nuevas[c.claveCapa()] = &c;
    // Las que se van: salen con su fundido.
    for (auto& [k, r] : dsk_)
        if (nuevas.count(k) == 0 && !r->saliendo) {
            r->saliendo = true;
            r->alfaDesde = r->alfa;
            r->alfaHacia = 0;
            r->fundeDesdeNs = ahora;
            r->fundeNs = dur;
        }
    for (const auto& [k, c] : nuevas) {
        Rama* r = nullptr;
        if (auto it = dsk_.find(k); it != dsk_.end() && c->sombra.hace() && it->second->sombra == nullptr) {
            // Le hace falta la sombra: se rearma la rama (una vez).
            soltar(*it->second);
            delete it->second;
            dsk_.erase(it);
        }
        if (auto it = dsk_.find(k); it != dsk_.end()) {
            r = it->second;
        } else {
            r = crearRama(*c, 0, dskMx_);
            if (r == nullptr) continue;
            dsk_[k] = r;
            r->alfa = 0;
        }
        Capa arriba = *c;
        arriba.orden = 100 + std::clamp(c->orden, 0, 899); // siempre encima de la escena (sink_0)
        ponerCapa(*r, arriba, r->alfa);
        r->saliendo = false;
        r->alfaDesde = r->alfa;
        r->alfaHacia = c->visible ? c->alfa : 0.0;
        r->fundeDesdeNs = ahora;
        r->fundeNs = dur;
    }
    return true;
}

void Composicion::tic() {
    std::lock_guard<std::mutex> l(mu_);
    if (pipeline_ == nullptr) return;
    aplicarLatencia();
    if (ahoraNs() - consultaNs_ > 1'000'000'000LL) {
        consultaNs_ = ahoraNs();
        // En otro hilo: ver LatenciaAparte (Piezas.hpp).
        if (const std::int64_t ns = latencia_.consultar(pipeline_); ns >= 0) salidaNs_ = ns;
    }
    // La transición.
    if (genVieja_ >= 0) {
        const double t = manual_ ? manualT_
                                 : std::clamp(static_cast<double>(ahoraNs() - transDesdeNs_) /
                                                  static_cast<double>(std::max<std::int64_t>(1, transNs_)),
                                              0.0, 1.0);
        bool nuevasListas = true;
        for (auto& [k, r] : ramas_)
            if (r->generacion == generacion_) nuevasListas = nuevasListas && r->listo.load();
        if (transicion_ != "corte") animar(t);
        const bool termino = manual_ ? false : transicion_ != "corte" ? t >= 1.0 : (nuevasListas || t >= 1.0);
        if (termino) {
            // Lo nuevo, como tiene que quedar (posición, ancho, recorte y alfa).
            for (auto& [k, r] : ramas_)
                if (r->generacion == generacion_) ponerCapa(*r, r->capa, r->alfaHacia);
            sacarGeneracion(genVieja_);
            genVieja_ = -1;
        }
    }
    // GL: la fuente negoció (o cambió) su tamaño: el recorte, que va en fracciones de él, de nuevo.
    // No en medio de una transición (la cortina usa el recorte): al terminar, ponerCapa lo pone.
    if (genVieja_ < 0)
        for (auto* m : {&ramas_, &dsk_})
            for (auto& [k, r] : *m)
                if (r->tamanoNuevo.exchange(false)) {
                    const Capa& c = r->capa;
                    // La sonda avisa cuando PASA el evento de caps, antes de que el pad las tenga: si
                    // el tic llega justo ahí, todavía no hay tamaño y no se puede recortar. Se vuelve a
                    // intentar en la próxima vuelta; antes la marca se perdía y la capa quedaba sin
                    // recortar para siempre (una de dos capas de la misma cámara mostraba la toma entera).
                    if (!recorteGl(*r, c, 0, c.ancho > 0 ? c.ancho : lienzo_.ancho, c.alto > 0 ? c.alto : lienzo_.alto))
                        r->tamanoNuevo = true;
                }
    // Los fundidos del DSK.
    for (auto it = dsk_.begin(); it != dsk_.end();) {
        Rama* r = it->second;
        const double t = r->fundeNs <= 0 ? 1.0
                                         : std::clamp(static_cast<double>(ahoraNs() - r->fundeDesdeNs) / static_cast<double>(r->fundeNs), 0.0, 1.0);
        r->alfa = r->alfaDesde + (r->alfaHacia - r->alfaDesde) * t;
        g_object_set(r->pad, "alpha", r->alfa, nullptr);
        if (r->saliendo && t >= 1.0) {
            soltar(*r);
            delete r;
            it = dsk_.erase(it);
            continue;
        }
        ++it;
    }
    GstBus* bus = gst_element_get_bus(pipeline_);
    while (GstMessage* m = gst_bus_pop(bus)) {
        if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_ERROR) {
            GError* e = nullptr;
            gst_message_parse_error(m, &e, nullptr);
            error_ = e != nullptr ? ba::diag::utf8Valido(e->message) : "error";
            registrar(Nivel::Error, "composición " + nombre_ + " (" + GST_OBJECT_NAME(GST_MESSAGE_SRC(m)) + "): " + error_);
            if (e != nullptr) g_error_free(e);
        } else if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_LATENCY) {
            recalcularLatenciaAparte(pipeline_);
        }
        gst_message_unref(m);
    }
    gst_object_unref(bus);
}

std::int64_t Composicion::latenciaSalidaNs() const {
    std::lock_guard<std::mutex> l(mu_);
    return std::max(salidaNs_, latenciaNs_);
}

json Composicion::estado() const {
    std::lock_guard<std::mutex> l(mu_);
    json capas = json::array();
    for (const auto& [k, r] : ramas_)
        capas.push_back({{"entrada", r->entrada},
                         {"generacion", r->generacion},
                         {"llega", r->listo.load()},
                         {"edad_llega", r->llega.json()}});
    return {{"bus", nombre_},
            {"anda", pipeline_ != nullptr},
            {"gpu", gpu_ ? (gl_ ? "gl" : "d3d12") : "cpu"},
            {"error", error_},
            {"escena", escena_.nombre},
            {"en_transicion", genVieja_ >= 0},
            {"dsk", [&]() {
                 json d = json::array();
                 for (const auto& [k, r] : dsk_) d.push_back({{"capa", k}, {"alfa", r->alfa}, {"saliendo", r->saliendo}});
                 return d;
             }()},
            {"latencia_ms", static_cast<double>(latenciaNs_) / 1e6},
            {"latencia_salida_ms", static_cast<double>(std::max(salidaNs_, latenciaNs_)) / 1e6},
            {"edad_salida", edadSalida_.json()},
            {"margen_ms", static_cast<double>(ajustes::msANs("video.margenComposicionMs", 5)) / 1e6},
            {"cuadros_tarde_60s", [&]() {
                 int n = 0;
                 for (const auto& [k, r] : ramas_) n += r->llega.tarde60s();
                 return n;
             }()},
            {"capas", capas}};
}

bool Composicion::ponerFiltros(const json& filtros) {
    const std::vector<Filtro> fv = leerFiltros(filtros);
    if (firmaDe(fv, {}, false) != firmaFiltros_) return false;
    return pipeline_ == nullptr || ponerEnVivo(pipeline_, fv, "ba_c_");
}

GstElement* Composicion::pipelineRef() const {
    std::lock_guard<std::mutex> l(mu_);
    return pipeline_ != nullptr ? GST_ELEMENT(gst_object_ref(pipeline_)) : nullptr;
}

} // namespace ba::motor
