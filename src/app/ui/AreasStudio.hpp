// ============================================================================
// src/app/ui/AreasStudio.hpp — Los ViewModels de área de StudioViewModel, todos juntos
//
// StudioViewModel.hpp los declara (no los incluye: tocarlo no recompila a quien solo usa
// uno de ellos; Ola 3, docs/local/PLAN-DISENO-FISICO.md). Quien usa los accesos de Studio a
// sus áreas (Studio.aire(), Studio.asamblea(), Studio.salidas()…) incluye este header o
// el del área que usa. El moc de StudioViewModel lo ve por Q_MOC_INCLUDE.
// ============================================================================
#pragma once

#include "ui/AppViewModel.hpp"
#include "ui/AsambleaViewModel.hpp"
#include "ui/AsistenteViewModel.hpp"
#include "ui/ControlViewModel.hpp"
#include "ui/EmisionViewModel.hpp"
#include "ui/EstadoAireViewModel.hpp"
#include "ui/EscritorioViewModel.hpp"
#include "ui/EscenasViewModel.hpp"
#include "ui/ImagenViewModel.hpp"
#include "ui/MezclaViewModel.hpp"
#include "ui/SalaViewModel.hpp"
#include "ui/SalidasViewModel.hpp"
#include "ui/MultiviewViewModel.hpp"
