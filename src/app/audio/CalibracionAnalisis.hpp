// ============================================================================
// src/app/audio/CalibracionAnalisis.hpp — las cuentas de la calibración de sala
//
// Sin Qt ni audio: curvas, micrófonos, promedio espacial, modos, propuesta y
// verificación sobre el JSON de una sesión (audio::Calibracion lo guarda y lo
// mide). Ver docs/planes/PLAN-CALIBRACION-SALA.md: qué se corrige y qué no.
//
// Las curvas van en una grilla fija de 1/24 de octava de 20 Hz a 20 kHz (240
// puntos), en dB (escala relativa de la medición: sirven las diferencias).
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <utility>
#include <vector>

namespace ba::app::audio::calib {

using json = nlohmann::json;
using Curva = std::vector<double>;

/// Las frecuencias de la grilla (240 puntos, 1/24 de octava, 20 Hz…20 kHz).
const std::vector<double>& grilla();
/// Índice del punto de la grilla más cercano a `f`.
size_t indiceDe(double f);

/// Interpolación en frecuencia logarítmica de una tabla (f, dB) ordenada; fuera
/// de la tabla, el extremo.
double interpolar(const std::vector<std::pair<double, double>>& tabla, double f);
/// Un archivo de calibración de micrófono (el formato de REW/miniDSP/Dayton:
/// líneas «frecuencia dB [fase]», comentarios con " o *). Vacío si no sirve.
std::vector<std::pair<double, double>> leerArchivoCalibracion(const std::string& texto);

/// Los modelos con curva publicada (aproximada): [{id, nombre, nota}].
json modelosMicrofono();
/// La respuesta del micrófono sobre la grilla (dB, lo que se le RESTA a lo medido).
/// mic: {calibracion: {tipo: plano|modelo|archivo|sustitucion, modelo, puntos: [[f, dB]…]}}.
Curva curvaMicrofono(const json& mic);
/// Hasta dónde se le cree al micrófono: {desde, detalle_hasta, tendencia_hasta} (Hz).
/// Detalle: filtros angostos; tendencia: solo curvas anchas; arriba, nada.
json confianzaMicrofono(const json& mic);

/// Suavizado de 1/n de octava (promedio de potencia).
Curva suavizar(const Curva& c, double n);
/// Promedio de potencia de varias curvas (no el promedio en dB).
Curva promedioPotencia(const std::vector<Curva>& cs);
/// Frecuencia de Schroeder: 2000·√(RT60/V).
double frecuenciaSchroeder(double rt60, double volumen);
/// Los modos de una sala rectangular hasta `fmax`: [{f, n: [x,y,z], tipo, de}].
json modosTeoricos(double largo, double ancho, double alto, double fmax);
/// La respuesta (dB) de un filtro de la propuesta sobre la grilla.
/// f: {tipo: pico|estante_graves|estante_agudos, f, ganancia_db, q}.
Curva respuestaFiltro(const json& f, double frecuencia = 48000);
/// La curva objetivo: plana con `pendiente` dB/oct (negativa: cae hacia los agudos)
/// centrada en 1 kHz, a nivel `ref`.
Curva curvaObjetivo(double ref, double pendiente);

/// El análisis de la sesión: rt por banda, fs, y por parlante el promedio espacial
/// (corregido por los micrófonos), los modos (presentes en la mayoría de los
/// puntos), los pozos, la llegada y el nivel en la zona de escucha.
json analizar(const json& sesion);
/// La propuesta (con su motivo, sin aplicar nada): por parlante filtros (cortes
/// en los modos bajo fs; pocos filtros anchos arriba, +3/-6 dB como mucho, nada
/// donde el micrófono no es confiable), retardos, niveles y polaridad.
json proponer(const json& sesion, const json& analisis);
/// Antes contra después: la desviación (RMS, dB) contra el objetivo con las
/// mediciones de etapa «antes» y las de «verificacion», por parlante.
json verificar(const json& sesion, const json& analisis);

} // namespace ba::app::audio::calib
