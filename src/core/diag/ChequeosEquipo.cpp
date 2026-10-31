// src/core/diag/ChequeosEquipo.cpp — ver ChequeosEquipo.hpp

#include "diag/ChequeosEquipo.hpp"

#include "i18n/I18n.hpp"

#include <cmath>
#include <map>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif
#if defined(__linux__)
#include <sys/xattr.h>
#endif

namespace ba::diag {

using ba::i18n::llenar;
using ba::i18n::traducir;
using platform::ItemPreparacion;

namespace {

/// La familia de un codificador por su fábrica: las sesiones se cuentan por familia
/// (h264 y h265 de NVENC comparten el tope).
std::string familia(const std::string& f) {
    if (f.rfind("nv", 0) == 0) return "nvenc";
    if (f.rfind("qsv", 0) == 0) return "qsv";
    if (f.rfind("amf", 0) == 0) return "amf";
    if (f.rfind("vtenc", 0) == 0) return "videotoolbox";
    if (f.rfind("d3d12", 0) == 0) return "d3d12";
    if (f.rfind("x264", 0) == 0 || f.rfind("x265", 0) == 0) return "cpu";
    return f;
}

std::string texto(double v, int dec) {
    const double f = std::pow(10.0, dec);
    std::string s = std::to_string(std::round(v * f) / f);
    if (const auto p = s.find('.'); p != std::string::npos) s.resize(dec > 0 ? p + 1 + static_cast<std::size_t>(dec) : p);
    return s;
}

std::string nombrePlaca(const nlohmann::json& p) {
    const std::string e = p.value("entrada", std::string()), d = p.value("dispositivo", std::string());
    if (!e.empty() && !d.empty()) return e + " (" + d + ")";
    return !e.empty() ? e : !d.empty() ? d : traducir("equipo", "una placa");
}

std::string unir(const std::vector<std::string>& v) {
    std::string out;
    for (const auto& s : v) out += (out.empty() ? "" : ", ") + s;
    return out;
}

ItemPreparacion sesiones(const DatosEquipo& d) {
    ItemPreparacion it{"codificador_sesiones", "no_aplica", traducir("equipo", "Sesiones del codificador de video"), {}, {}};
    std::map<std::string, int> porFamilia;
    for (const auto& c : d.codificaciones)
        if (c.is_object() && c.value("tipo", std::string("video")) == "video") ++porFamilia[familia(c.value("fabrica", std::string()))];
    if (porFamilia.empty()) {
        it.detalle = traducir("equipo", "Ahora no hay ninguna abierta: se mira con las salidas andando (cada salida con su perfil abre una; la copia de la grabación comparte la de la grabación).");
        return it;
    }
    it.estado = "ok";
    std::vector<std::string> partes;
    for (const auto& [fam, n] : porFamilia) {
        const int tope = d.limiteSesiones > 0 ? d.limiteSesiones : limiteSesionesDe(fam);
        if (tope > 0) {
            partes.push_back(llenar(traducir("equipo", "%1: %2 de %3"), {fam, std::to_string(n), std::to_string(tope)}));
            if (n >= tope) it.estado = "falta";
            else if (n + 1 >= tope && it.estado == "ok") it.estado = "revisar";
        } else {
            partes.push_back(llenar(traducir("equipo", "%1: %2 (sin tope fijo)"), {fam, std::to_string(n)}));
        }
    }
    it.detalle = llenar(traducir("equipo", "Abiertas ahora: %1."), {unir(partes)});
    if (it.estado == "falta")
        it.como = traducir("equipo", "Llegaste al tope de la placa: la próxima salida no va a arrancar. Juntá salidas con el mismo perfil (comparten el codificador), pasá una a x264 (CPU) o subí el tope en «equipo.limiteSesiones» si tu placa admite más (las profesionales no tienen tope).");
    else if (it.estado == "revisar")
        it.como = traducir("equipo", "Queda una sola sesión libre: antes de sumar otra salida, mirá si puede compartir el perfil de una que ya está.");
    return it;
}

ItemPreparacion audio48(const DatosEquipo& d) {
    ItemPreparacion it{"audio_48k", "no_aplica", traducir("equipo", "Placas de audio a 48 kHz"), {}, {}};
    std::vector<std::string> otras, sinSaber;
    int n = 0;
    for (const auto& p : d.placas) {
        if (!p.is_object()) continue;
        ++n;
        const int hz = p.value("hz", 0);
        if (hz <= 0) sinSaber.push_back(nombrePlaca(p));
        else if (hz != 48000) otras.push_back(llenar(traducir("equipo", "%1 a %2 Hz"), {nombrePlaca(p), std::to_string(hz)}));
    }
    if (n == 0) {
        it.detalle = traducir("equipo", "No hay placas de audio abiertas ahora.");
        return it;
    }
    if (!otras.empty()) {
        it.estado = "falta";
        it.detalle = llenar(traducir("equipo", "No están a 48 kHz: %1. Se remuestrean en cada bloque (más CPU y más latencia)."), {unir(otras)});
        it.como = traducir("equipo", "Ponelas a 48 kHz en el panel de la placa (macOS: Configuración de Audio MIDI; Windows: Sonido → Propiedades → Opciones avanzadas; Linux: el perfil de PipeWire) y volvé a abrir la entrada.");
    } else if (!sinSaber.empty()) {
        it.estado = "revisar";
        it.detalle = llenar(traducir("equipo", "Todavía no se sabe la frecuencia de: %1 (se ve cuando llega audio)."), {unir(sinSaber)});
    } else {
        it.estado = "ok";
        it.detalle = llenar(traducir("equipo", "Las %1 placas abiertas están a 48 kHz."), {std::to_string(n)});
    }
    return it;
}

ItemPreparacion ptp(const DatosEquipo& d) {
    ItemPreparacion it{"ptp", "no_aplica", traducir("equipo", "Reloj PTP"), {}, {}};
    if (d.relojTipo != "ptp") {
        it.detalle = traducir("equipo", "El estudio no usa PTP. PTP (IEEE 1588) sirve para que varias placas o aparatos (Dante, AES67) cuenten el mismo tiempo; escucha en los puertos 319 y 320, que el sistema reserva a administradores, por eso el ayudante gst-ptp-helper necesita permisos.");
        return it;
    }
    if (!d.relojError.empty()) {
        it.estado = "falta";
        it.detalle = llenar(traducir("equipo", "PTP no arrancó: %1. El estudio sigue con el reloj del sistema."), {d.relojError});
    } else {
        it.estado = "ok";
        it.detalle = traducir("equipo", "El estudio cuenta el tiempo con PTP.");
    }
    if (!d.ptpAyudante) {
        if (it.estado == "ok") it.estado = "revisar";
        it.como = traducir("equipo", "Falta gst-ptp-helper en el paquete (libexec/gstreamer-1.0): sin él PTP no puede escuchar.");
    } else if (d.ptpPermisos == 0) {
        if (it.estado == "ok") it.estado = "revisar";
        it.como = traducir("equipo", "Dale permisos al ayudante una vez, como administrador: en Linux `sudo setcap cap_net_bind_service,cap_net_admin+ep libexec/gstreamer-1.0/gst-ptp-helper`; en macOS `sudo chown root libexec/gstreamer-1.0/gst-ptp-helper && sudo chmod u+s libexec/gstreamer-1.0/gst-ptp-helper`.");
    } else if (d.ptpPermisos < 0 && it.estado != "ok") {
        it.como = traducir("equipo", "En Windows, permitile al programa la red privada en el firewall (puertos UDP 319 y 320).");
    }
    return it;
}

ItemPreparacion deriva(const DatosEquipo& d) {
    ItemPreparacion it{"deriva", "no_aplica", traducir("equipo", "Placas enganchadas a la hora del estudio"), {}, {}};
    std::vector<std::string> mal, esperando;
    int n = 0;
    for (const auto& p : d.placas) {
        if (!p.is_object()) continue;
        ++n;
        const double ppm = p.value("ppm", 0.0);
        const auto reanclajes = p.value("reanclajes", std::uint64_t{0});
        if (reanclajes > 0)
            mal.push_back(llenar(traducir("equipo", "%1: %2 cortes en el programa"), {nombrePlaca(p), std::to_string(reanclajes)}));
        else if (std::fabs(ppm) >= 100)
            mal.push_back(llenar(traducir("equipo", "%1: %2 ppm"), {nombrePlaca(p), texto(ppm, 1)}));
        else if (p.value("medido_s", 0.0) < 60 || (p.value("corregida", false) && !p.value("enganchada", false)))
            esperando.push_back(nombrePlaca(p));
    }
    if (n == 0) {
        it.detalle = traducir("equipo", "No hay placas de audio abiertas ahora.");
        return it;
    }
    if (!mal.empty()) {
        it.estado = "falta";
        it.detalle = llenar(traducir("equipo", "Se apartan de la hora del estudio: %1."), {unir(mal)});
        it.como = traducir("equipo", "Una placa sana se aparta menos de 100 ppm. Si tenés word clock o PTP, enganchá la placa ahí; si no, revisá que no esté a otra frecuencia o compartida con otro programa.");
    } else if (!esperando.empty()) {
        it.estado = "revisar";
        it.detalle = llenar(traducir("equipo", "Midiendo (hace falta un minuto de audio): %1."), {unir(esperando)});
    } else {
        it.estado = "ok";
        it.detalle = llenar(traducir("equipo", "Las %1 placas siguen la hora del estudio sin cortes."), {std::to_string(n)});
    }
    return it;
}

} // namespace

int permisosDeAyudantePtp(const std::filesystem::path& ayudante) {
#if defined(_WIN32)
    (void)ayudante;
    return -1; // en Windows no hacen falta privilegios: lo que manda es el firewall
#else
    struct stat st {};
    if (::stat(ayudante.c_str(), &st) != 0) return -1;
    if ((st.st_mode & S_ISUID) != 0 && st.st_uid == 0) return 1;
#if defined(__linux__)
    // setcap deja las capacidades en este atributo extendido.
    if (::getxattr(ayudante.c_str(), "security.capability", nullptr, 0) > 0) return 1;
#endif
    return 0;
#endif
}

int limiteSesionesDe(const std::string& fabrica) {
    // NVENC de las GeForce: 8 sesiones por máquina desde los controladores de 2024
    // (antes 3 y 5); las Quadro/RTX profesionales no tienen. Lo demás depende de la
    // placa o de la CPU: sin un número que valga para todos.
    return familia(fabrica) == "nvenc" ? 8 : -1;
}

std::vector<ItemPreparacion> chequeosDeEquipo(const DatosEquipo& d) { return {sesiones(d), audio48(d), ptp(d), deriva(d)}; }

} // namespace ba::diag
