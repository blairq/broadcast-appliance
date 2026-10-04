// src/app/agent/Subtitulos.cpp — ver Subtitulos.hpp

#include "agent/Subtitulos.hpp"

#include "agent/FlujosTexto.hpp"

#include <obs.h>

#include <cstring>
#include <string>

namespace ba::app::agent {

namespace {
/// El proc global no se puede sacar de libobs: apunta acá mientras exista el objeto.
Subtitulos* gActivo = nullptr;
FlujosTexto* gFlujos = nullptr;

void listaFlujos(void*, calldata_t* cd) {
    std::string l;
    if (gFlujos != nullptr)
        for (const QVariant& v : gFlujos->flujos())
            l += v.toMap().value("nombre").toString().toStdString() + "\n";
    calldata_set_string(cd, "lista", l.c_str());
}

struct Busqueda {
    std::string flujo;
    std::string texto;
    int cuantos = 0;
};

bool cadaFuente(void* param, obs_source_t* s) {
    auto* b = static_cast<Busqueda*>(param);
    const char* id = obs_source_get_unversioned_id(s);
    if (id == nullptr || std::strcmp(id, "ba_subtitulos") != 0) return true;
    obs_data_t* d = obs_source_get_settings(s);
    const bool es = b->flujo == obs_data_get_string(d, "flujo");
    obs_data_release(d);
    if (!es) return true;
    ++b->cuantos;
    if (!b->texto.empty()) {
        calldata_t cd;
        calldata_init(&cd);
        calldata_set_string(&cd, "texto", b->texto.c_str());
        proc_handler_call(obs_source_get_proc_handler(s), "agregar", &cd);
        calldata_free(&cd);
    }
    return true;
}
} // namespace

Subtitulos::Subtitulos(FlujosTexto& flujos, QObject* parent) : QObject(parent), flujos_(flujos) {
    static bool registrado = false;
    gActivo = this;
    gFlujos = &flujos;
    if (!registrado) {
        proc_handler_add(obs_get_proc_handler(), "void ba_flujos_texto(out string lista)", listaFlujos,
                         nullptr);
        registrado = true;
    }
    connect(&flujos_, &FlujosTexto::frase, this, &Subtitulos::alFrase);
}

Subtitulos::~Subtitulos() {
    if (gActivo == this) {
        gActivo = nullptr;
        gFlujos = nullptr;
    }
}

void Subtitulos::alFrase(const QString& flujo, const QString& texto) {
    Busqueda b{flujo.toStdString(), texto.toStdString()};
    obs_enum_sources(cadaFuente, &b);
}

int Subtitulos::cuadrosDe(const QString& flujo) const {
    Busqueda b{flujo.toStdString(), {}};
    obs_enum_sources(cadaFuente, &b);
    return b.cuantos;
}

} // namespace ba::app::agent
