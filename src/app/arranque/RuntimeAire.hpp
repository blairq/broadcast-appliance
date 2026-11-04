// ============================================================================
// src/app/arranque/RuntimeAire.hpp — AppRuntime y lo que sale, sin el motor
//
// Para quien usa del runtime lo que está al aire y no el motor: el documento de aire
// (documentoAire, aireAnterior), las salidas guardadas (emision/SalidasConfig), lo
// pedido de cada salida (emision/PedidosSalida), la foto (ultimaFoto, aportarALaFoto),
// el latido (latido), el feed del operador (operatorFeed), el objetivo de loudness, la
// salud y el canal del launcher. Ver arranque/RuntimeConfig.hpp.
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include "arranque/RuntimeConfig.hpp"
#include "audio/ObjetivoLoudness.hpp"
#include "emision/DocumentoAire.hpp"
#include "emision/FotoMotor.hpp"
#include "emision/LatidoAire.hpp"
#include "emision/OperatorFeed.hpp"
#include "emision/PedidosSalida.hpp"
#include "emision/SalidasConfig.hpp"
#include "ipc/ControlService.hpp"
#include "ipc/HealthBlock.hpp"
#include "persist/ArchivoConfig.hpp"
