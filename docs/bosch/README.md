# Bosch DCN Next Generation

Todo lo del sistema de conferencia Bosch DCN NG del recinto, en un solo lugar.

## Lo que escribimos nosotros (en git)

| Documento | De qué trata |
|---|---|
| [open-interface.md](open-interface.md) | Cómo la app controla la CCU por el Open Interface. Cubre el transporte serie Full/Simple y TCP, los mensajes y el catálogo de funciones, más el plugin `bosch-dcn`, las herramientas `dcn_*`, la interfaz y la botonera. Al final, un apéndice con las funciones sin documentar |
| [dcn-ddi.md](dcn-ddi.md) | La DCN-DDI (interfaz de los micrófonos de banca): modos, direccionamiento, paneles, cómo se ve en la app, planilla para relevar la sala y cómo reemplazar una |

El código está en `assets/plugins/bosch-dcn/` (protocolo, simulador y pruebas),
`src/app/legislatura/ConferenciaDcn.*`, `src/app/mcp/McpDcnTools.cpp` y
`src/app/qml/controls/PanelConferencia.qml`.

## Lo que vino en el DVD de Bosch (`dvd/`, fuera de git)

Copiado del «Customer DVD» del sistema. Es material de Bosch: **no se commitea** (`docs/bosch/dvd/`
está en `.gitignore`), se guarda solo en esta máquina y en las copias de respaldo.

```
dvd/
├── firmware/                         lo que la «Download and License Tool» le baja a los equipos
│   ├── dlt-3.1x/                     DLT 3.1x: firmware 3.10.3974 (DLF.VERSION.TXT)
│   │   ├── ccu/                      DLF.CCU.ABS (la CCU de primera generación, la nuestra),
│   │   │                             DLF.DCN_CCU.UIM, DLF.DCN_CCUFS.UIM
│   │   ├── ccu2/                     DLF.CCU2.OMF
│   │   ├── pantallas-idiomas/        DLF.DUS*G.ABS: un archivo por idioma de las pantallas de las unidades
│   │   ├── interpretes-y-unidades/   INT_TX04/08/16/32 (transmisores), DPRS_* (expansores de audio, CobraNet)
│   │   ├── fpga/                     MIF_3.BIT, MMU_3.BIT
│   │   └── otros/                    DUNE.BIM, VERSION.TXT, CONFIG.INI (idiomas de las CCU esclavas)
│   ├── dlt/                          la DLT más nueva del DVD (archivos de 2011–2012)
│   │   ├── ccu/                      DCN_CCU.uim
│   │   ├── ccu2/                     ccu2master, cargador y software de la esclava, sysroot
│   │   ├── unidad-inalambrica/       wu.*, Wap*.uim (punto de acceso y unidades DCN-W)
│   │   ├── interpretes-y-unidades/   INT_TX*, DPRS_*
│   │   ├── fpga/                     bridge*.bmm
│   │   └── herramientas/             data2mem, spiflash, MmpFlashTool, UPTKDriver
│   └── cobranet/                     prsp311_2_21_10.bin (firmware de la placa CobraNet)
├── manuales/
│   ├── instalacion-y-operacion/      IUI_DCN_<idioma>.pdf (es, en, de, fr, it, nl, pt, pl, ru, tr, ja, ko, th, zh)
│   │   └── version-3.1x/             los mismos de la versión 3.1x
│   ├── open-interface/               OI_en-4.0.pdf, OI_en-3.1x.pdf, OIQRC.pdf (tarjeta de referencia),
│   │                                 bosch-dcn-ng-open-interface-4.1.pdf y -4.7.pdf
│   ├── software-pc-3.1x/             un manual por módulo del software de PC: AT asistencia, CC cámaras,
│   │                                 DB delegados, IC intercom, IN interpretación, LD pantallas,
│   │                                 MD/MM micrófonos, MV mensajes, PV votación, SI instalación,
│   │                                 SM sinóptico, ST tiempos, SW_II instalación del software, VD video
│   ├── conference-suite-dcn-sw/      DCN-SW_<idioma>.pdf + manual del Presentation Client
│   ├── camaras-sacc/                 DCN-SWSACC (control automático de cámaras)
│   ├── multimedia-dcn-mr/            DCN-MR (grabación multimedia)
│   ├── interpretes/                  IUI-INT_* y el pupitre IDESK6
│   ├── inalambrico/                  QIC_DCN-W_* (sistema inalámbrico) y QRC_DCN-WD
│   ├── guias-rapidas/                QRC_DCN-* de cada unidad (CON, CONCM, CONCS, CONFF, DIS*, Flush, Idesk)
│   ├── herramienta-de-descarga-dlt/  IUI_DLT_en.pdf, DCN-DLT.chm
│   ├── sinoptico-smd/                DCN-SWSMD.pdf
│   ├── notas-de-version/             RLN_DCN_en-4.0.pdf, RLN_DCN_en-3.1x.pdf
│   └── seguridad/                    Safety_ml.pdf
├── librerias/                        las librerías de comunicación con la CCU del software de Bosch
│   ├── ddtk/                         CCU_DDTK.exe (kit de desarrollo del OI) y FNID.DEF (códigos de función)
│   ├── dcn-pc-3.1x/                  SCS.DLL (enlace serie Full/Simple), DCNNGAPI.DLL + dcnngapi.h,
│   │                                 COMMAN/LAN/ACN/CUS/DM/ics y el servidor (SERVER.EXE, DCNNGOLE.EXE)
│   ├── sinoptico-smv/                ensamblados .NET Bosch.Dcn.OpenInterface.* / Communication / Operational
│   │                                 (nombres truncados por el MSI; de acá salen los enums del apéndice)
│   ├── dcn-sw-conference-suite/      Server.OPENINTERFACE.*, Server.COMMUNICATION, EcpcClient.*, ClientApi.*
│   ├── camaras-sacc/                 Bosch.Dcn.Communication, Bosch.Oil.* (Open Interface Library), Vml.Driver
│   ├── dcn-swapi/                    DCN-SWAPI.chm + ejemplo en C# (API de la Conference Suite)
│   ├── dcn-swsmd-ejemplo/            fuentes C# del cliente de ejemplo de la Conference Suite
│   └── dlt-herramienta-de-descarga/  DLT.DOWNLOAD.EXE y sus DLL (y version-3.1x/)
└── planos/                           DWG (Concentus, Discussion, Interpreter), EPS de las guías rápidas,
                                      planillas de cálculo (DCNNGW_CT_5.1/6.1, INT-DSC, INT-FPC)
```

### Qué no está

- **Instaladores completos** (setup.exe, .msi, .NET, SQL Express): siguen en el DVD original, en
  `~/Documents/Software DCN Bosch_Juan de Dobrzynski/Customer DVD/`.
- **Un programa para bajar el firmware de la CCU a la PC**: no existe en ningún lado del DVD.
  El Open Interface solo tiene funciones de descarga hacia la CCU (las DL del apéndice, que la app
  deja comentadas a propósito).

### Advertencia

No hace falta grabarle firmware a la CCU para usar la app: la app habla el Open Interface que la CCU
ya trae. Si alguna vez hay que regrabarla, se hace con la DLT de Bosch y el firmware de la **misma**
versión que figura en el menú de la CCU. Un firmware equivocado deja la central inservible, y no hay
repuesto.
