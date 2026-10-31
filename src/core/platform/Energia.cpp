// ============================================================================
// src/core/platform/Energia.cpp — lo común del corte de luz (ver Energia.hpp)
//
// La decisión (decidirEnergia), la lectura de /sys/class/power_supply (que la usa
// Energia_linux.cpp y se prueba en cualquier sistema con carpetas falsas) y el
// punto del sistema de archivos de la tarjeta de preparación.
// ============================================================================
#include "platform/Energia.hpp"
#include "i18n/I18n.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>

namespace ba::platform {

namespace {

std::string leerLinea(const std::filesystem::path& p) {
    std::ifstream f(p);
    std::string s;
    std::getline(f, s);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

long long leerNumero(const std::filesystem::path& p) {
    const std::string s = leerLinea(p);
    if (s.empty()) return -1;
    try {
        return std::stoll(s);
    } catch (...) {
        return -1;
    }
}

std::string minusculas(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

} // namespace

AccionEnergia decidirEnergia(const EstadoEnergia& e, const PoliticaEnergia& p) {
    if (!e.conocido || !e.enBateria) return AccionEnergia::Nada;
    const bool poco = (e.porcentaje >= 0 && e.porcentaje <= p.porcentaje) || (e.minutos >= 0 && e.minutos <= p.minutos);
    return poco && p.cerrarGrabaciones ? AccionEnergia::CerrarGrabaciones : AccionEnergia::Avisar;
}

EstadoEnergia leerPowerSupply(const std::filesystem::path& dir) {
    EstadoEnergia e;
    std::error_code ec;
    bool hayRed = false, redOnline = false, descargando = false;
    int sumaPct = 0, cuantas = 0;
    long long minutos = -1;
    for (const auto& ent : std::filesystem::directory_iterator(dir, ec)) {
        const std::filesystem::path d = ent.path();
        const std::string tipo = minusculas(leerLinea(d / "type"));
        if (tipo == "mains" || tipo == "usb" || tipo == "usb_c" || tipo == "usb_pd") {
            hayRed = true;
            if (leerNumero(d / "online") == 1) redOnline = true;
            continue;
        }
        if (tipo != "battery" && tipo != "ups") continue;
        // La batería del mouse o del joystick no es la de la máquina.
        if (minusculas(leerLinea(d / "scope")) == "device") continue;
        if (leerNumero(d / "present") == 0) continue;
        e.conocido = e.hayBateria = true;
        if (tipo == "ups" || e.fuente.empty()) e.fuente = tipo == "ups" ? "ups" : "bateria";
        const std::string st = minusculas(leerLinea(d / "status"));
        if (st == "discharging") descargando = true;
        if (const long long c = leerNumero(d / "capacity"); c >= 0) {
            sumaPct += static_cast<int>(std::min<long long>(c, 100));
            ++cuantas;
        }
        // Lo que queda: el núcleo lo da directo (time_to_empty_now, segundos) o se
        // calcula con la energía y la potencia (µWh / µW) o la carga y la corriente.
        long long seg = leerNumero(d / "time_to_empty_now");
        if (seg < 0) {
            const long long en = leerNumero(d / "energy_now"), pot = leerNumero(d / "power_now");
            const long long ca = leerNumero(d / "charge_now"), co = leerNumero(d / "current_now");
            if (en > 0 && pot > 0) seg = en * 3600 / pot;
            else if (ca > 0 && co > 0) seg = ca * 3600 / co;
        }
        if (seg > 0) minutos = std::max<long long>(minutos, 0) + seg / 60;
    }
    if (hayRed) e.conocido = true;
    e.enBateria = e.hayBateria && (descargando || (hayRed && !redOnline));
    e.porcentaje = cuantas > 0 ? sumaPct / cuantas : -1;
    e.minutos = e.enBateria && minutos >= 0 ? static_cast<int>(minutos) : -1;
    return e;
}

ItemPreparacion itemSistemaDeArchivos(const std::filesystem::path& datos) {
    using ba::i18n::llenar;
    using ba::i18n::traducir;
    ItemPreparacion it;
    it.id = "sistema_archivos";
    it.titulo = traducir("preparacion", "Sistema de archivos con diario");
    const std::string fs = sistemaDeArchivos(datos);
    const std::string m = minusculas(fs);
    // Con diario: un corte a mitad de una escritura no rompe el disco (las
    // grabaciones van en fragmentos que se abren igual, pero la tabla del disco no).
    static const char* const kConDiario[] = {"apfs", "hfs", "ntfs", "refs", "ext3", "ext4", "xfs", "btrfs", "zfs", "f2fs", "jfs"};
    static const char* const kSinDiario[] = {"msdos", "fat", "vfat", "fat32", "exfat", "ext2"};
    if (fs.empty()) {
        it.estado = "revisar";
        it.detalle = traducir("preparacion", "No se pudo saber el sistema de archivos de la carpeta de datos.");
    } else if (std::any_of(std::begin(kConDiario), std::end(kConDiario), [&](const char* k) { return m.rfind(k, 0) == 0; })) {
        it.estado = "ok";
        it.detalle = llenar(traducir("preparacion", "Los datos están en %1, que tiene diario."), {fs});
    } else if (std::any_of(std::begin(kSinDiario), std::end(kSinDiario), [&](const char* k) { return m.rfind(k, 0) == 0; })) {
        it.estado = "falta";
        it.detalle = llenar(traducir("preparacion", "Los datos están en %1, sin diario: un corte en mitad de una escritura "
                                                    "puede dañar el disco entero, no solo el último archivo."),
                            {fs});
        it.como = traducir("preparacion", "Usá un disco formateado en APFS (Mac), NTFS (Windows) o ext4 (Linux), o copiá la "
                                          "carpeta del programa a un disco interno.");
    } else {
        it.estado = "revisar";
        it.detalle = llenar(traducir("preparacion", "Los datos están en %1: no sabemos si tiene diario."), {fs});
    }
    return it;
}

ItemPreparacion itemRespaldo(const EstadoEnergia& e) {
    using ba::i18n::llenar;
    using ba::i18n::traducir;
    ItemPreparacion it;
    it.id = "respaldo";
    it.titulo = traducir("preparacion", "Batería o UPS");
    if (e.hayBateria) {
        it.estado = "ok";
        it.detalle = e.fuente == "ups" ? traducir("preparacion", "El sistema ve un UPS: un corte avisa y las grabaciones se cierran en orden.")
                                       : traducir("preparacion", "El equipo tiene batería: un corte avisa y las grabaciones se cierran en orden.");
        if (e.porcentaje >= 0) it.detalle += " " + llenar(traducir("preparacion", "Ahora: %1 %."), {std::to_string(e.porcentaje)});
    } else {
        it.estado = "falta";
        it.detalle = traducir("preparacion", "El sistema no ve ninguna batería ni UPS: un corte apaga la máquina en seco "
                                             "(la grabación se retoma al volver, pero el último fragmento puede perderse).");
        it.como = traducir("preparacion", "Conectá un UPS con su cable USB de datos (el sistema lo reconoce solo, sin "
                                          "instalar nada).");
    }
    return it;
}

} // namespace ba::platform
