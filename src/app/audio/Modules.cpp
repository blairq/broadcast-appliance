// ============================================================================
// src/app/audio/Modules.cpp — ver Modules.hpp
// ============================================================================

#include "audio/Modules.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>

#include "i18n/I18n.hpp"

// Las tablas se marcan para lupdate y se traducen al mostrarlas. Sin Qt (los tests de
// audio enlazan este archivo sin Qt), la marca es el texto tal cual.
#ifndef QT_TRANSLATE_NOOP
#define QT_TRANSLATE_NOOP(contexto, texto) texto
#endif

namespace ba::app::audio {

using json = nlohmann::json;

namespace {

std::string lower(std::string s) {
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string withBand(const std::string& name, int n) {
    std::string out = name;
    const auto p = out.find("{n}");
    if (p != std::string::npos) out.replace(p, 3, std::to_string(n));
    return out;
}

std::vector<ModuleType> build() {
    std::vector<ModuleType> c;
    const ModuleParam inGain{"entrada_db", "Input gain", QT_TRANSLATE_NOOP("modulos", "Ganancia de entrada (dB).")};
    const ModuleParam outGain{"salida_db", "Output gain", QT_TRANSLATE_NOOP("modulos", "Ganancia de salida (dB).")};
    const ModuleParam bypass{"bypass", "Bypass", QT_TRANSLATE_NOOP("modulos", "Desactivado (true/false).")};
    {
        ModuleType t;
        t.key = "gate";
        t.aliases = {"compuerta", "noise_gate"};
        t.pluginId = "in.lsp-plug.gate_stereo";
        t.title = QT_TRANSLATE_NOOP("modulos", "Compuerta (gate)");
        t.help =
            QT_TRANSLATE_NOOP("modulos", "Cierra el canal cuando la señal baja del umbral (ruido de fondo entre frases). Calibrar: umbral "
            "6-10 dB sobre el piso de ruido y bien debajo de la voz; reduccion -20..-40 dB.");
        t.params = {
            {"umbral_db", "Curve threshold", QT_TRANSLATE_NOOP("modulos", "Por debajo de este nivel (dBFS) cierra.")},
            {"zona_db", "Curve zone size", QT_TRANSLATE_NOOP("modulos", "Transicion abierta-cerrada (dB, negativo).")},
            {"reduccion_db", "Reduction", QT_TRANSLATE_NOOP("modulos", "Cuanto atenua cerrada (dB, negativo; -inf = silencio).")},
            {"ataque_ms", "Attack", QT_TRANSLATE_NOOP("modulos", "Tiempo de apertura (ms).")},
            {"liberacion_ms", "Release", QT_TRANSLATE_NOOP("modulos", "Tiempo de cierre (ms).")},
            {"sosten_ms", "Hold time", QT_TRANSLATE_NOOP("modulos", "Espera antes de cerrar (ms).")},
            {"histeresis", "Hysteresis", QT_TRANSLATE_NOOP("modulos", "Umbral de cierre distinto al de apertura (on/off).")},
            {"umbral_histeresis_db", "Hysteresis threshold", QT_TRANSLATE_NOOP("modulos", "Umbral de cierre (dBFS) con histeresis.")},
            {"detector_pasa_altos", "High-pass filter mode", QT_TRANSLATE_NOOP("modulos", "Filtro del detector (off, 12/24/36 dB/oct).")},
            {"detector_pasa_altos_hz", "High-pass filter frequency", QT_TRANSLATE_NOOP("modulos", "Corte del filtro del detector (Hz).")},
            inGain,
            outGain,
            bypass};
        t.presets = {{"voz",
                      {{"umbral_db", -45},
                       {"reduccion_db", -30},
                       {"ataque_ms", 5},
                       {"liberacion_ms", 150},
                       {"sosten_ms", 60},
                       {"detector_pasa_altos", "12 dB/oct"},
                       {"detector_pasa_altos_hz", 100}}}};
        c.push_back(t);
    }
    {
        ModuleType t;
        t.key = "compresor";
        t.aliases = {"compressor", "comp"};
        t.pluginId = "in.lsp-plug.compressor_stereo";
        t.title = QT_TRANSLATE_NOOP("modulos", "Compresor");
        t.help = QT_TRANSLATE_NOOP("modulos", "Reduce la dinamica: lo que pasa el umbral se atenua segun el ratio. Voz: ratio 2-4, ataque "
                 "5-20 ms, liberacion 80-200 ms, 3-6 dB de reduccion en las partes fuertes.");
        t.params = {{"umbral_db", "Attack threshold", QT_TRANSLATE_NOOP("modulos", "Umbral (dBFS).")},
                    {"ratio", "Ratio", QT_TRANSLATE_NOOP("modulos", "Relacion de compresion (1..100).")},
                    {"rodilla_db", "Knee", QT_TRANSLATE_NOOP("modulos", "Rodilla (dB, negativo = mas suave).")},
                    {"ataque_ms", "Attack time", QT_TRANSLATE_NOOP("modulos", "Ataque (ms).")},
                    {"liberacion_ms", "Release time", QT_TRANSLATE_NOOP("modulos", "Liberacion (ms).")},
                    {"ganancia_db", "Makeup gain", QT_TRANSLATE_NOOP("modulos", "Recuperacion de ganancia (dB).")},
                    inGain,
                    outGain,
                    bypass};
        t.presets = {
            {"voz",
             {{"umbral_db", -20},
              {"ratio", 3},
              {"rodilla_db", -6},
              {"ataque_ms", 10},
              {"liberacion_ms", 120},
              {"ganancia_db", 3}}},
            {"musica_fondo", {{"umbral_db", -24}, {"ratio", 2}, {"ataque_ms", 30}, {"liberacion_ms", 250}}}};
        c.push_back(t);
    }
    {
        ModuleType t;
        t.key = "deesser";
        t.aliases = {"de-esser", "sibilancia"};
        t.pluginId = "in.lsp-plug.deesser_stereo";
        t.title = QT_TRANSLATE_NOOP("modulos", "De-esser");
        t.help =
            QT_TRANSLATE_NOOP("modulos", "Atenua las sibilancias (s, ch). La deteccion arranca en `deteccion_hz` (4-8 kHz segun la voz); "
            "`escuchar` deja oir solo lo que detecta para ajustar.");
        t.params = {
            {"umbral_db", "Threshold", QT_TRANSLATE_NOOP("modulos", "Umbral de la sibilancia detectada (dBFS).")},
            {"ratio", "Ratio", QT_TRANSLATE_NOOP("modulos", "Cuanto la atenua.")},
            {"rodilla_db", "Knee", QT_TRANSLATE_NOOP("modulos", "Rodilla (dB).")},
            {"ataque_ms", "Attack time", QT_TRANSLATE_NOOP("modulos", "Ataque (ms).")},
            {"liberacion_ms", "Release time", QT_TRANSLATE_NOOP("modulos", "Liberacion (ms).")},
            {"deteccion_hz", "High-pass filter frequency", QT_TRANSLATE_NOOP("modulos", "Desde que frecuencia detecta (Hz).")},
            {"escuchar", "Sidechain listen", QT_TRANSLATE_NOOP("modulos", "Escuchar solo lo detectado (on/off). Apagar al terminar.")},
            inGain,
            outGain,
            bypass};
        t.presets = {
            {"voz", {{"umbral_db", -26}, {"ratio", 4}, {"deteccion_hz", 5500}, {"liberacion_ms", 40}}}};
        c.push_back(t);
    }
    {
        ModuleType t;
        t.key = "eq";
        t.aliases = {"ecualizador", "equalizer", "filtro", "filtro_multibanda"};
        t.pluginId = "in.lsp-plug.para_equalizer_x8_stereo";
        t.title = QT_TRANSLATE_NOOP("modulos", "Ecualizador parametrico (8 bandas)");
        t.help =
            QT_TRANSLATE_NOOP("modulos", "Filtros por banda (0-7): tipo (Off, Bell, Hi-pass, Lo-pass, Hi-shelf, Lo-shelf, Notch...), "
            "frecuencia, ganancia y Q. Voz tipica: pasa-altos 80 Hz, -2..-4 dB en 200-300 Hz si embarra, +2 "
            "dB en 3-5 kHz para claridad. Medir con audio_measure (respuesta).");
        t.params = {inGain, outGain, bypass};
        t.bandParams = {{"tipo", "Filter type {n}", QT_TRANSLATE_NOOP("modulos", "Tipo de filtro.")},
                        {"modo", "Filter mode {n}", QT_TRANSLATE_NOOP("modulos", "Modo (RLC, BWC, LRX, APO...).")},
                        {"pendiente", "Filter slope {n}", QT_TRANSLATE_NOOP("modulos", "Pendiente (x1..x4).")},
                        {"frecuencia_hz", "Frequency {n}", QT_TRANSLATE_NOOP("modulos", "Frecuencia (Hz).")},
                        {"ganancia_db", "Gain {n}", QT_TRANSLATE_NOOP("modulos", "Ganancia (dB).")},
                        {"q", "Quality factor {n}", QT_TRANSLATE_NOOP("modulos", "Q (ancho: mas alto = mas angosto).")}};
        t.bandFirst = 0;
        t.bandCount = 8;
        t.presets = {
            {"voz",
             {{"bandas",
               {{{"banda", 0}, {"tipo", "Hi-pass"}, {"frecuencia_hz", 80}},
                {{"banda", 1}, {"tipo", "Bell"}, {"frecuencia_hz", 250}, {"ganancia_db", -2}, {"q", 1}},
                {{"banda", 2}, {"tipo", "Bell"}, {"frecuencia_hz", 4000}, {"ganancia_db", 2}, {"q", 1}}}}}}};
        c.push_back(t);
    }
    {
        ModuleType t;
        t.key = "multibanda";
        t.aliases = {"compresor_multibanda", "mb_compressor", QT_TRANSLATE_NOOP("modulos", "mbcomp")};
        t.pluginId = "in.lsp-plug.mb_compressor_stereo";
        t.title = QT_TRANSLATE_NOOP("modulos", "Compresor multibanda");
        t.help =
            QT_TRANSLATE_NOOP("modulos", "Comprime cada banda por separado (hasta 8). Las bandas 2-8 se activan con `activa` y empiezan "
            "en `corte_hz`; la banda 1 va desde abajo.");
        t.params = {{"modo", "Compressor mode", QT_TRANSLATE_NOOP("modulos", "Classic / Modern / Linear Phase.")}, inGain, outGain, bypass};
        t.bandParams = {{"activa", "Compression band enable {n}", QT_TRANSLATE_NOOP("modulos", "Banda activa (2-8).")},
                        {"corte_hz", "Split frequency {n}", QT_TRANSLATE_NOOP("modulos", "Frecuencia donde empieza la banda (2-8).")},
                        {"umbral_db", "Attack threshold {n}", QT_TRANSLATE_NOOP("modulos", "Umbral (dBFS).")},
                        {"ratio", "Ratio {n}", QT_TRANSLATE_NOOP("modulos", "Ratio.")},
                        {"ataque_ms", "Attack time {n}", QT_TRANSLATE_NOOP("modulos", "Ataque (ms).")},
                        {"liberacion_ms", "Release time {n}", QT_TRANSLATE_NOOP("modulos", "Liberacion (ms).")},
                        {"ganancia_db", "Makeup gain {n}", QT_TRANSLATE_NOOP("modulos", "Recuperacion (dB).")}};
        t.bandFirst = 1;
        t.bandCount = 8;
        c.push_back(t);
    }
    {
        ModuleType t;
        t.key = "limitador";
        t.aliases = {"limiter", "techo"};
        t.pluginId = "in.lsp-plug.limiter_stereo";
        t.title = QT_TRANSLATE_NOOP("modulos", "Limitador");
        t.help = QT_TRANSLATE_NOOP("modulos", "Techo que la señal no pasa (al final de la cadena). Transmision: -1 dBFS.");
        t.params = {{"umbral_db", "Threshold", QT_TRANSLATE_NOOP("modulos", "Techo (dBFS).")},
                    {"lookahead_ms", "Lookahead", QT_TRANSLATE_NOOP("modulos", "Anticipacion (ms).")},
                    {"ataque_ms", "Attack time", QT_TRANSLATE_NOOP("modulos", "Ataque (ms).")},
                    {"liberacion_ms", "Release time", QT_TRANSLATE_NOOP("modulos", "Liberacion (ms).")},
                    {"modo", "Operating mode", QT_TRANSLATE_NOOP("modulos", "Modo (Herm Thin, Exp...).")},
                    inGain,
                    outGain,
                    bypass};
        t.presets = {{"seguridad", {{"umbral_db", -1}, {"lookahead_ms", 5}}}};
        c.push_back(t);
    }
    return c;
}

bool isNumberText(const std::string& s) {
    char* end = nullptr;
    std::strtod(s.c_str(), &end);
    return end != s.c_str();
}

} // namespace

const std::vector<ModuleType>& moduleCatalog() {
    static const std::vector<ModuleType> c = build();
    return c;
}

const ModuleType* findModule(const std::string& keyOrPluginId) {
    const std::string k = lower(keyOrPluginId);
    for (const auto& t : moduleCatalog()) {
        if (t.key == k || t.pluginId == keyOrPluginId) return &t;
        for (const auto& a : t.aliases)
            if (a == k) return &t;
    }
    return nullptr;
}

const ParamInfo* findParam(const std::vector<ParamInfo>& ps, const std::string& nameOrId) {
    const std::string n = lower(nameOrId);
    for (const auto& p : ps)
        if (lower(p.name) == n) return &p;
    for (const auto& p : ps)
        if (std::to_string(p.id) == nameOrId) return &p;
    return nullptr;
}

json paramJson(ClapInstance& inst, const ParamInfo& p) {
    double v = 0;
    inst.getValue(p.id, v);
    json j{{"nombre", p.name}, {"valor", v}, {"texto", inst.valueText(p.id, v)}};
    if (p.isEnum() || (p.stepped() && p.max - p.min <= 24)) {
        json opts = json::array();
        for (double k = p.min; k <= p.max + 1e-9; k += 1)
            opts.push_back(inst.valueText(p.id, k));
        j["opciones"] = opts;
    } else {
        j["rango"] = {inst.valueText(p.id, p.min), inst.valueText(p.id, p.max)};
    }
    return j;
}

std::string setParam(ClapInstance& inst, const ParamInfo& p, const json& v) {
    if (p.readOnly()) return ba::i18n::traducir("modulos", "es de solo lectura");
    double out = 0;
    std::string text;
    if (v.is_boolean()) {
        out = v.get<bool>() ? p.max : p.min;
        return inst.setValue(p.id, out) ? "" : ba::i18n::traducir("modulos", "cola llena");
    }
    if (v.is_number()) {
        // Unidades reales por la conversion del plugin; si no la acepta, el
        // valor tal cual si esta en el rango.
        std::ostringstream os;
        os << v.get<double>();
        text = os.str();
    } else if (v.is_string()) {
        text = v.get<std::string>();
    } else {
        return ba::i18n::traducir("modulos", "valor invalido");
    }
    // Listas (tipo de filtro, modo...): por el texto de cada opcion.
    if ((p.isEnum() || p.stepped()) && !isNumberText(text) && p.max - p.min <= 64) {
        const std::string want = lower(text);
        for (double k = p.min; k <= p.max + 1e-9; k += 1) {
            const std::string o = lower(inst.valueText(p.id, k));
            if (o == want || o.rfind(want, 0) == 0) return inst.setValue(p.id, k) ? "" : ba::i18n::traducir("modulos", "cola llena");
        }
        std::string opts;
        for (double k = p.min; k <= p.max + 1e-9; k += 1)
            opts += (opts.empty() ? "" : ", ") + inst.valueText(p.id, k);
        return ba::i18n::llenar(ba::i18n::traducir("modulos", "opcion desconocida '%1' (validas: %2)"), {text, opts});
    }
    if (text == "on" || text == "true") return inst.setValue(p.id, p.max) ? "" : ba::i18n::traducir("modulos", "cola llena");
    if (text == "off" || text == "false") return inst.setValue(p.id, p.min) ? "" : ba::i18n::traducir("modulos", "cola llena");
    if (inst.textToValue(p.id, text, out)) {
        out = std::clamp(out, p.min, p.max);
        return inst.setValue(p.id, out) ? "" : ba::i18n::traducir("modulos", "cola llena");
    }
    if (isNumberText(text)) {
        out = std::strtod(text.c_str(), nullptr);
        if (out < p.min - 1e-9 || out > p.max + 1e-9)
            return ba::i18n::llenar(ba::i18n::traducir("modulos", "fuera de rango (%1 .. %2)"),
                                       {inst.valueText(p.id, p.min), inst.valueText(p.id, p.max)});
        return inst.setValue(p.id, out) ? "" : ba::i18n::traducir("modulos", "cola llena");
    }
    return ba::i18n::llenar(ba::i18n::traducir("modulos", "no se entiende '%1'"), {text});
}

json readModule(ClapInstance& inst, const ModuleType& t) {
    const auto ps = inst.params();
    json out = json::object();
    for (const auto& mp : t.params)
        if (const ParamInfo* p = findParam(ps, mp.lspName)) {
            json j = paramJson(inst, *p);
            j["ayuda"] = mp.help;
            out[mp.key] = j;
        }
    if (t.bandCount > 0) {
        json bands = json::array();
        for (int b = t.bandFirst; b < t.bandFirst + t.bandCount; ++b) {
            json bj{{"banda", b}};
            for (const auto& mp : t.bandParams)
                if (const ParamInfo* p = findParam(ps, withBand(mp.lspName, b))) {
                    double v = 0;
                    inst.getValue(p->id, v);
                    bj[mp.key] = inst.valueText(p->id, v);
                }
            bands.push_back(bj);
        }
        out["bandas"] = bands;
        // Opciones de los parametros por banda que son listas (tipo de filtro...).
        json opts = json::object();
        for (const auto& mp : t.bandParams)
            if (const ParamInfo* p =
                    findParam(ps, withBand(mp.lspName, t.bandFirst + (t.key == "multibanda" ? 1 : 0))))
                if (p->isEnum() || (p->stepped() && p->max - p->min <= 24)) {
                    json o = json::array();
                    for (double k = p->min; k <= p->max + 1e-9; k += 1)
                        o.push_back(inst.valueText(p->id, k));
                    opts[mp.key] = o;
                }
        out["opcionesBandas"] = opts;
    }
    return out;
}

json applyModule(ClapInstance& inst, const ModuleType* t, const json& values) {
    const auto ps = inst.params();
    json applied = json::array();
    json errors = json::object();
    auto apply = [&](const std::string& label, const std::string& lspName, const json& v) {
        const ParamInfo* p = findParam(ps, lspName);
        if (p == nullptr) {
            errors[label] = ba::i18n::traducir("modulos", "no existe el parametro");
            return;
        }
        const std::string e = setParam(inst, *p, v);
        if (e.empty())
            applied.push_back(label);
        else
            errors[label] = e;
    };
    for (auto it = values.begin(); it != values.end(); ++it) {
        const std::string key = it.key();
        if (key == "bandas" && t != nullptr && t->bandCount > 0) {
            for (const auto& bj : it.value()) {
                const int b = bj.value("banda", -1);
                if (b < t->bandFirst || b >= t->bandFirst + t->bandCount) {
                    errors["bandas"] = ba::i18n::llenar(ba::i18n::traducir("modulos", "banda fuera de rango (%1..%2)"),
                                                       {std::to_string(t->bandFirst),
                                                        std::to_string(t->bandFirst + t->bandCount - 1)});
                    continue;
                }
                for (auto bi = bj.begin(); bi != bj.end(); ++bi) {
                    if (bi.key() == "banda") continue;
                    bool known = false;
                    for (const auto& mp : t->bandParams)
                        if (mp.key == bi.key()) {
                            apply("banda " + std::to_string(b) + "." + mp.key, withBand(mp.lspName, b),
                                  bi.value());
                            known = true;
                        }
                    if (!known)
                        errors["banda " + std::to_string(b) + "." + bi.key()] = ba::i18n::traducir("modulos", "parametro desconocido");
                }
            }
            continue;
        }
        bool known = false;
        if (t != nullptr)
            for (const auto& mp : t->params)
                if (mp.key == key) {
                    apply(key, mp.lspName, it.value());
                    known = true;
                }
        if (!known) apply(key, key, it.value()); // nombre LSP directo
    }
    return json{{"aplicados", applied}, {"errores", errors}};
}

} // namespace ba::app::audio
