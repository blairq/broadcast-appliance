// ============================================================================
// src/app/audio/LspDsp.hpp — la biblioteca ba-lspdsp (el DSP de LSP), cargada en
// tiempo de ejecución
//
// ba-lspdsp (src/lspdsp/ba_lspdsp.h) se compila con el compilador de LSP y tiene
// una API en C: acá se abre (dlopen / LoadLibrary) y se resuelven sus funciones.
// Si no está (o es de otra versión de la API), api() da nullptr y quien la use
// sigue sin ella (el mezclador mide con el Analyzer propio).
//
// Dónde la busca: $BA_LSPDSP, bin/64bit del bundle (junto a los ejecutables), y
// en desarrollo extern/lsp/<plataforma>/dsp (BA_DEV_LSPDSP).
// ============================================================================
#pragma once

#include "ba_lspdsp.h"

#include <string>

namespace ba::app::audio::lspdsp {

struct Api {
    int (*version)();
    const char* (*versionLsp)();

    ba_medidores* (*medidoresCrear)(int canales, int frecuencia);
    void (*medidoresProcesar)(ba_medidores*, const float* izq, const float* der, size_t muestras);
    void (*medidoresLeer)(ba_medidores*, ba_medidores_lectura* out);
    void (*medidoresReiniciarIntegrado)(ba_medidores*);
    void (*medidoresLiberar)(ba_medidores*);

    ba_analizador* (*analizadorCrear)(int canales, int frecuencia, int rango);
    void (*analizadorConfigurar)(ba_analizador*, int rango, int ventana, int envolvente, float reactividad);
    void (*analizadorProcesar)(ba_analizador*, const float* const* canales, size_t muestras);
    int (*analizadorLeer)(ba_analizador*, int canal, float desde, float hasta, size_t n, float* frq, float* db);
    void (*analizadorLiberar)(ba_analizador*);

    ba_ruido* (*ruidoCrear)(int frecuencia, int color, int generador, float amplitud);
    void (*ruidoGenerar)(ba_ruido*, float* dst, size_t muestras);
    void (*ruidoLiberar)(ba_ruido*);

    ba_latencia* (*latenciaCrear)(int frecuencia, float duracion_s);
    void (*latenciaArrancar)(ba_latencia*);
    void (*latenciaProcesar)(ba_latencia*, float* salida, const float* entrada, size_t muestras);
    int (*latenciaResultado)(ba_latencia*, float* ms);
    void (*latenciaLiberar)(ba_latencia*);
};

/// La biblioteca (se abre la primera vez), o nullptr si no está o es de otra versión.
const Api* api();
/// Por qué no está (vacío si está), para el diagnóstico y el MCP.
std::string error();
/// De dónde se cargó.
std::string ruta();

} // namespace ba::app::audio::lspdsp
