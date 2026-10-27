// ============================================================================
// src/app/resolucion/Plan.cpp — ver Plan.hpp
// ============================================================================
#include "resolucion/Plan.hpp"

#include "i18n/I18n.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ba::app::resolucion {

using json = nlohmann::json;
using ba::i18n::llenar;
using ba::i18n::traducir;

namespace {

std::string tipoDe(const std::string& elemento) {
    if (elemento.rfind("baptz", 0) == 0) return "ptz";
    if (elemento == "basuperres" || elemento == "bavtsuperres" || elemento == "bafsr" || elemento == "baonnxsr" ||
        elemento == "bacoremlsr" || elemento == "baglsl")
        return "superres";
    if (elemento == "basalida") return "salida";
    if (elemento == "barostros" || elemento == "baseguir") return "analiza";
    if (elemento == "barecorte" || elemento == "videocrop") return "recorte";
    return "filtro";
}

std::string txt(const Tam& t) { return std::to_string(t.ancho) + "×" + std::to_string(t.alto); }
json js(const Tam& t) { return {{"ancho", t.ancho}, {"alto", t.alto}}; }
Tam escalar(const Tam& t, double f) {
    return {static_cast<int>(std::lround(t.ancho * f)), static_cast<int>(std::lround(t.alto * f))};
}
Tam minimo(const Tam& a, const Tam& b) {
    if (a.vacio()) return b;
    if (b.vacio()) return a;
    return {std::min(a.ancho, b.ancho), std::min(a.alto, b.alto)};
}
/// La escala con que la escena lo muestra (la del lado que manda al ajustar): < 1 = se achica.
double escalaEnEscena(const Tam& llega, const Tam& se) {
    if (llega.vacio() || se.vacio()) return 1;
    return std::max(static_cast<double>(se.ancho) / llega.ancho, static_cast<double>(se.alto) / llega.alto);
}
/// Se achica a menos de 2/3: lo que sobra ya se nota en lo que cuesta.
bool deMas(const Tam& llega, const Tam& se) { return escalaEnEscena(llega, se) < 0.67; }

std::string num(double x) {
    char b[16];
    std::snprintf(b, sizeof b, std::fabs(x - std::round(x)) < 0.05 ? "%.0f" : "%.1f", x);
    return b;
}

} // namespace

int peso(const std::string& nivel) {
    if (nivel == "mal") return 3;
    if (nivel == "mal_armado") return 2;
    if (nivel == "mejorable") return 1;
    return 0;
}

Tam demandaDe(const Entrada& e) {
    Tam d;
    for (const auto& [escena, t] : e.seVe) {
        d.ancho = std::max(d.ancho, t.ancho);
        d.alto = std::max(d.alto, t.alto);
    }
    return d;
}

json planear(const Entrada& e) {
    const Tam D = demandaDe(e);
    json escenas = json::array();
    for (const auto& [escena, t] : e.seVe) escenas.push_back({{"escena", escena}, {"ancho", t.ancho}, {"alto", t.alto}});
    json out{{"entrada", e.id}, {"demanda", {{"ancho", D.ancho}, {"alto", D.alto}, {"escenas", escenas}}}, {"origen", js(e.origen)}};
    json hallazgos = json::array();
    json niveles = json::object();
    const auto hallazgo = [&](const char* tipo, const char* nivel, const std::string& texto, const std::string& tarjeta,
                              json arreglo = nullptr) {
        json h{{"tipo", tipo}, {"nivel", nivel}, {"texto", texto}};
        if (!tarjeta.empty()) h["tarjeta"] = tarjeta;
        if (!arreglo.is_null()) h["arreglo"] = arreglo;
        hallazgos.push_back(h);
        const std::string k = tarjeta.empty() ? "entrada" : tarjeta;
        json& n = niveles[k];
        if (!n.is_object()) n = {{"nivel", nivel}, {"motivos", json::array()}};
        if (peso(nivel) > peso(n.value("nivel", std::string()))) n["nivel"] = nivel;
        n["motivos"].push_back(texto);
    };

    // --- Para adelante: lo que entra, sale y el detalle real en cada paso ----------------
    struct Paso {
        const Tarjeta* t;
        std::string tipo;
        Tam entra, sale, real;
    };
    std::vector<Paso> pasos;
    Tam cur = e.origen, real = e.origen;
    for (const auto& t : e.cadena) {
        const std::string tipo = tipoDe(t.elemento);
        if (tipo == "salida") continue; // una rama: no cambia lo que sigue
        Paso p{&t, tipo, cur, t.sale, real};
        if (p.sale.vacio()) p.sale = tipo == "superres" ? escalar(cur, 2) : cur;
        if (tipo == "ptz" && t.zoom > 1.001) real = escalar(real, 1.0 / t.zoom);
        if (tipo != "superres") real = minimo(real, p.sale); // achicar pierde; agrandar no suma
        p.real = real;
        pasos.push_back(p);
        cur = p.sale;
    }
    const Tam llega = cur; // lo que recibe la composición

    // --- Para atrás: lo que necesita cada paso para entregar lo que se ve ----------------
    Tam necesita = D;
    std::vector<Tam> necesitaEn(pasos.size());
    for (size_t i = pasos.size(); i-- > 0;) {
        necesitaEn[i] = necesita; // lo que tiene que entregar
        if (pasos[i].tipo == "superres") necesita = escalar(necesita, 0.5);
        else if (pasos[i].tipo == "ptz") necesita = escalar(necesita, std::max(1.0, pasos[i].t->zoom));
    }
    const Tam necesitaFuente = necesita;

    json jp = json::array();
    for (size_t i = 0; i < pasos.size(); ++i) {
        const auto& p = pasos[i];
        jp.push_back({{"id", p.t->id}, {"titulo", p.t->titulo}, {"tipo", p.tipo}, {"elemento", p.t->elemento},
                      {"entra", js(p.entra)}, {"sale", js(p.sale)}, {"real", js(p.real)}, {"necesita", js(necesitaEn[i])}});
    }
    out["pasos"] = jp;
    out["llega"] = js(llega);

    if (D.vacio()) {
        out["nota"] = traducir("resolucion", "no está en ninguna escena: no se puede decir cuánto hace falta");
        out["hallazgos"] = hallazgos;
        out["niveles"] = niveles;
        return out;
    }

    // --- Orden ----------------------------------------------------------------------------
    int iSr = -1, iPtz = -1;
    for (size_t i = 0; i < pasos.size(); ++i) {
        if (pasos[i].tipo == "superres" && iSr < 0) iSr = static_cast<int>(i);
        if (pasos[i].tipo == "ptz" && iPtz < 0) iPtz = static_cast<int>(i);
    }
    if (iSr >= 0 && iPtz > iSr)
        hallazgo("orden_ptz_superres", "mal_armado",
                 traducir("resolucion", "El PTZ está después de la superresolución: recorta píxeles que la superres "
                                        "inventó. Va antes: la superres tiene que recibir la ventana real de la cámara."),
                 pasos[iPtz].t->id, {{"accion", "reordenar"}, {"tarjeta", pasos[iPtz].t->id}, {"antes_de", pasos[iSr].t->id}});

    // Un filtro que solo trabaja en RAM entre tarjetas de GPU: parte la cadena.
    if (e.cadenaGpu)
        for (size_t i = 0; i < pasos.size(); ++i) {
            const auto& p = pasos[i];
            if (!p.t->soloRam || p.tipo == "superres") continue;
            bool gpuAntes = false, gpuDespues = false;
            for (size_t k = 0; k < i; ++k) gpuAntes = gpuAntes || pasos[k].t->gpuPosible;
            for (size_t k = i + 1; k < pasos.size(); ++k) gpuDespues = gpuDespues || pasos[k].t->gpuPosible;
            if (gpuAntes && gpuDespues)
                hallazgo("parte_la_cadena", "mal_armado",
                         llenar(traducir("resolucion", "«%1» solo trabaja en RAM y está entre tarjetas que van en la GPU: el "
                                                       "video baja y vuelve a subir en cada cuadro. Movelo al principio o al "
                                                       "final de la cadena."),
                                {p.t->titulo}),
                         p.t->id, {{"accion", "mover"}, {"tarjeta", p.t->id}, {"a", "final"}});
        }

    // --- La superres: que haga falta y que reciba lo que tiene que recibir -----------------
    if (iSr >= 0) {
        const auto& p = pasos[iSr];
        if (p.sale.ancho <= p.entra.ancho && p.sale.alto <= p.entra.alto)
            hallazgo("superres_no_agranda", "mejorable",
                     llenar(traducir("resolucion", "La superresolución no está agrandando: entra y sale %1 (el motor no "
                                                   "acepta ese tamaño —el de Apple llega a 960×960— o está en reposo). En "
                                                   "esta cadena sobra; si hace falta, tiene que recibir %2."),
                            {txt(p.entra), txt(escalar(D, 0.5))}),
                     p.t->id, {{"accion", "apagar"}, {"tarjeta", p.t->id}});
        else if (p.entra.ancho >= D.ancho * 0.95 && p.entra.alto >= D.alto * 0.95)
            hallazgo("superres_sobra", "mejorable",
                     llenar(traducir("resolucion", "La superresolución agranda algo que ya cubre lo que se ve: entra %1, "
                                                   "sale %2 y la escena lo muestra como mucho a %3. Gasta la GPU en píxeles "
                                                   "que después se tiran."),
                            {txt(p.entra), txt(p.sale), txt(D)}),
                     p.t->id, {{"accion", "apagar"}, {"tarjeta", p.t->id}});
        else if (p.real.area() < p.entra.area() * 0.8)
            hallazgo("superres_estirado", "mejorable",
                     llenar(traducir("resolucion", "La superresolución recibe un recorte estirado (%1 con el detalle de "
                                                   "%2): rinde peor que con la ventana real. El PTZ debería entregarle %3."),
                            {txt(p.entra), txt(p.real), txt(escalar(necesitaEn[iSr].vacio() ? D : necesitaEn[iSr], 0.5))}),
                     p.t->id);
    }

    // --- El PTZ: el detalle real con el zoom actual -----------------------------------------
    if (iPtz >= 0) {
        const auto& p = pasos[iPtz];
        const double z = p.t->zoom;
        bool srDespues = false;
        for (size_t k = iPtz + 1; k < pasos.size(); ++k) srDespues = srDespues || pasos[k].tipo == "superres";
        if (z > 1.001 && p.real.area() < D.area() * 0.9) {
            const bool mucho = p.real.area() < D.area() / 9.0;
            std::string t = llenar(traducir("resolucion", "Con zoom %1 el PTZ toma una ventana de %2 de la cámara y la "
                                                          "estira a %3 (se ve a %4)."),
                                   {num(z), txt(p.real), txt(p.sale), txt(D)});
            if (!srDespues)
                t += " " + traducir("resolucion", "Una superresolución después del PTZ, o un modo más grande de la cámara, "
                                                  "devuelve detalle.");
            // Con superres después, perder detalle con poco zoom es lo esperado: la superres lo recupera.
            if (mucho || !srDespues) hallazgo("zoom_sin_detalle", mucho ? "mal" : "mejorable", t, p.t->id);
        }
        if (deMas(p.sale, D) && !srDespues)
            hallazgo("ptz_saca_de_mas", "mejorable",
                     llenar(traducir("resolucion", "El PTZ saca %1 y se ve a %2: podría entregar lo que se pide."),
                            {txt(p.sale), txt(D)}),
                     p.t->id);
    }

    // --- Lo que llega a la escena contra lo que se ve ----------------------------------------
    if (deMas(llega, D)) {
        // El responsable: el último paso que agrandó (la superres ya tiene su hallazgo); si
        // ninguno agrandó, la fuente (la de una cámara se ve abajo, con sus modos).
        const Paso* quien = nullptr;
        for (const auto& p : pasos)
            if (p.sale.area() > p.entra.area() * 1.1) quien = &p;
        if (quien != nullptr && quien->tipo != "superres")
            hallazgo("agranda_de_mas", "mejorable",
                     llenar(traducir("resolucion", "«%1» agranda a %2 y se ve como mucho a %3: se escala de más."),
                            {quien->t->titulo, txt(quien->sale), txt(D)}),
                     quien->t->id);
        else if (quien == nullptr && !e.esCamara)
            hallazgo("llega_de_mas", "mejorable",
                     llenar(traducir("resolucion", "Llega a la escena en %1 y se ve como mucho a %2: se procesa de más."),
                            {txt(llega), txt(D)}),
                     "");
    }

    // --- La fuente --------------------------------------------------------------------------
    out["fuente"] = {{"necesita", {{"ancho", necesitaFuente.ancho}, {"alto", necesitaFuente.alto}, {"cps", e.fps}}},
                     {"actual", e.modoActual.is_object() ? e.modoActual : json(js(e.origen))}};
    if (e.esCamara && !e.modos.empty()) {
        std::string porque;
        const ba::motor::ModoVideo rec = ba::motor::elegirModo(e.modos, {necesitaFuente.ancho, necesitaFuente.alto, e.fps}, &porque);
        if (rec.ancho > 0) {
            json r = rec.json();
            r["porque"] = porque;
            out["fuente"]["recomendado"] = r;
            const Tam recT{rec.ancho, rec.alto};
            const json arreglo{{"accion", "modo_camara"}, {"valor", rec.valor()}};
            if (!e.origen.vacio() && e.origen.area() > recT.area() * 1.4 && recT.ancho >= necesitaFuente.ancho)
                hallazgo("fuente_de_mas", "mejorable",
                         llenar(traducir("resolucion", "La cámara entrega %1 y para lo que se ve alcanza %2 (%3): se "
                                                       "captura, se decodifica y se escala de más."),
                                {txt(e.origen), txt(recT), rec.etiqueta()}),
                         "", arreglo);
            else if (!e.origen.vacio() && e.origen.ancho < necesitaFuente.ancho * 0.95 && recT.area() > e.origen.area() * 1.2)
                hallazgo("fuente_de_menos", "mejorable",
                         llenar(traducir("resolucion", "La cámara entrega %1 y hace falta %2: tiene %3."),
                                {txt(e.origen), txt(necesitaFuente), rec.etiqueta()}),
                         "",
                         arreglo);
            if (e.modoActual.is_object() && e.modoActual.value("convertido_por_el_sistema", false) && rec.formato != "crudo")
                hallazgo("convertido_por_el_sistema", "mejorable",
                         llenar(traducir("resolucion", "El modo actual lo decodifica el sistema en la CPU sin decirlo; %1 "
                                                       "hace lo mismo a la vista."),
                                {rec.etiqueta()}),
                         "", arreglo);
        }
    }
    if (!e.fija && e.origenFps > 0 && e.origenFps < e.fps - 1) {
        std::string t = llenar(traducir("resolucion", "La fuente da %1 cps y el lienzo pide %2: se repiten cuadros."),
                               {num(e.origenFps), num(e.fps)});
        json arreglo;
        if (e.esCamara)
            for (const auto& m : e.modos)
                if (m.usable() && m.fps >= e.fps - 0.5 && m.ancho >= std::min(e.origen.ancho, necesitaFuente.ancho)) {
                    t += " " + llenar(traducir("resolucion", "La cámara tiene %1."), {m.etiqueta()});
                    arreglo = {{"accion", "modo_camara"}, {"valor", m.valor()}};
                    break;
                }
        hallazgo("pocos_cps", e.origenFps < e.fps * 0.6 ? "mal" : "mejorable", t, "", arreglo);
    }

    std::stable_sort(hallazgos.begin(), hallazgos.end(),
                     [](const json& a, const json& b) { return peso(a.value("nivel", "")) > peso(b.value("nivel", "")); });
    out["hallazgos"] = hallazgos;
    out["niveles"] = niveles;
    return out;
}

} // namespace ba::app::resolucion
