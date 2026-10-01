/*
 * Copyright (C) 2026 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2026 Jorge Ortega <ortegajorge@gmail.com>
 *
 * This file is part of lsp-plugins-matrix-mixer
 * Created on: 1 oct 2026
 *
 * lsp-plugins-matrix-mixer is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * lsp-plugins-matrix-mixer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with lsp-plugins-matrix-mixer. If not, see <https://www.gnu.org/licenses/>.
 */

#include <lsp-plug.in/plug-fw/meta/ports.h>
#include <lsp-plug.in/plug-fw/meta/registry.h>
#include <lsp-plug.in/shared/meta/developers.h>
#include <private/meta/matrix_mixer.h>

#define LSP_PLUGINS_MATRIX_MIXER_VERSION_MAJOR       1
#define LSP_PLUGINS_MATRIX_MIXER_VERSION_MINOR       0
#define LSP_PLUGINS_MATRIX_MIXER_VERSION_MICRO       0

#define LSP_PLUGINS_MATRIX_MIXER_VERSION  \
    LSP_MODULE_VERSION( \
        LSP_PLUGINS_MATRIX_MIXER_VERSION_MAJOR, \
        LSP_PLUGINS_MATRIX_MIXER_VERSION_MINOR, \
        LSP_PLUGINS_MATRIX_MIXER_VERSION_MICRO  \
    )

namespace lsp
{
    namespace meta
    {
        //-------------------------------------------------------------------------
        // Plugin metadata

        // Audio ports
        #define MMIX_MONO_AUDIO_IN(id, label) \
            AUDIO_INPUT("in" id, "Audio input " label)

        #define MMIX_MONO_AUDIO_OUT(id, label) \
            AUDIO_OUTPUT("out" id, "Audio output " label)

        #define MMIX_STEREO_AUDIO_IN(id, label) \
            AUDIO_INPUT("in" id "l", "Audio input left " label), \
            AUDIO_INPUT("in" id "r", "Audio input right " label)

        #define MMIX_STEREO_AUDIO_OUT(id, label) \
            AUDIO_OUTPUT("out" id "l", "Audio output left " label), \
            AUDIO_OUTPUT("out" id "r", "Audio output right " label)

        // Input strip
        #define MMIX_INPUT_COMMON(id, label) \
            SWITCH("cs" id, "Input solo " label, "Solo in " label, 0.0f), \
            SWITCH("cm" id, "Input mute " label, "Mute in " label, 0.0f), \
            SWITCH("ci" id, "Input phase invert " label, "Phase in " label, 0.0f), \
            LOG_CONTROL("cg" id, "Input gain " label, "Gain in " label, U_GAIN_AMP, meta::matrix_mixer::CHANNEL_GAIN)

        #define MMIX_MONO_INPUT(id, label) \
            MMIX_INPUT_COMMON(id, label), \
            METER_GAIN("il" id, "Input level " label, GAIN_AMP_P_48_DB)

        #define MMIX_STEREO_INPUT(id, label) \
            MMIX_INPUT_COMMON(id, label), \
            METER_GAIN("il" id "l", "Input level left " label, GAIN_AMP_P_48_DB), \
            METER_GAIN("il" id "r", "Input level right " label, GAIN_AMP_P_48_DB)

        // Output strip
        #define MMIX_MONO_OUTPUT(id, label) \
            SWITCH("om" id, "Output mute " label, "Mute out " label, 0.0f), \
            LOG_CONTROL("og" id, "Output gain " label, "Gain out " label, U_GAIN_AMP, meta::matrix_mixer::CHANNEL_GAIN), \
            METER_GAIN("ol" id, "Output level " label, GAIN_AMP_P_48_DB)

        #define MMIX_STEREO_OUTPUT(id, label) \
            SWITCH("om" id, "Output mute " label, "Mute out " label, 0.0f), \
            PAN_CTL("ob" id, "Output balance " label, "Balance out " label, 0.0f), \
            LOG_CONTROL("og" id, "Output gain " label, "Gain out " label, U_GAIN_AMP, meta::matrix_mixer::CHANNEL_GAIN), \
            METER_GAIN("ol" id "l", "Output level left " label, GAIN_AMP_P_48_DB), \
            METER_GAIN("ol" id "r", "Output level right " label, GAIN_AMP_P_48_DB)

        // Crosspoint: input i routed to output o. By default the matrix is
        // the identity: each input goes only to the output of the same number.
        #define MMIX_CELL(i, il, o, ol, dfl) \
            LOG_CONTROL_DFL("mx" i o, "Matrix gain " il " to " ol, "Mix " il " to " ol, U_GAIN_AMP, meta::matrix_mixer::CELL_GAIN, dfl)

        #define MMIX_ROW_X4(i, il, d1, d2, d3, d4) \
            MMIX_CELL(i, il, "_1", "1", d1), \
            MMIX_CELL(i, il, "_2", "2", d2), \
            MMIX_CELL(i, il, "_3", "3", d3), \
            MMIX_CELL(i, il, "_4", "4", d4)

        #define MMIX_ROW_X8(i, il, d1, d2, d3, d4, d5, d6, d7, d8) \
            MMIX_ROW_X4(i, il, d1, d2, d3, d4), \
            MMIX_CELL(i, il, "_5", "5", d5), \
            MMIX_CELL(i, il, "_6", "6", d6), \
            MMIX_CELL(i, il, "_7", "7", d7), \
            MMIX_CELL(i, il, "_8", "8", d8)

        static const port_t matrix_mixer_x4_mono_ports[] =
        {
            MMIX_MONO_AUDIO_IN("_1", "1"),
            MMIX_MONO_AUDIO_IN("_2", "2"),
            MMIX_MONO_AUDIO_IN("_3", "3"),
            MMIX_MONO_AUDIO_IN("_4", "4"),
            MMIX_MONO_AUDIO_OUT("_1", "1"),
            MMIX_MONO_AUDIO_OUT("_2", "2"),
            MMIX_MONO_AUDIO_OUT("_3", "3"),
            MMIX_MONO_AUDIO_OUT("_4", "4"),
            BYPASS,

            MMIX_MONO_INPUT("_1", "1"),
            MMIX_MONO_INPUT("_2", "2"),
            MMIX_MONO_INPUT("_3", "3"),
            MMIX_MONO_INPUT("_4", "4"),

            MMIX_MONO_OUTPUT("_1", "1"),
            MMIX_MONO_OUTPUT("_2", "2"),
            MMIX_MONO_OUTPUT("_3", "3"),
            MMIX_MONO_OUTPUT("_4", "4"),

            MMIX_ROW_X4("_1", "1", GAIN_AMP_0_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB),
            MMIX_ROW_X4("_2", "2", GAIN_AMP_M_INF_DB, GAIN_AMP_0_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB),
            MMIX_ROW_X4("_3", "3", GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_0_DB, GAIN_AMP_M_INF_DB),
            MMIX_ROW_X4("_4", "4", GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_0_DB),

            PORTS_END
        };

        static const port_t matrix_mixer_x8_mono_ports[] =
        {
            MMIX_MONO_AUDIO_IN("_1", "1"),
            MMIX_MONO_AUDIO_IN("_2", "2"),
            MMIX_MONO_AUDIO_IN("_3", "3"),
            MMIX_MONO_AUDIO_IN("_4", "4"),
            MMIX_MONO_AUDIO_IN("_5", "5"),
            MMIX_MONO_AUDIO_IN("_6", "6"),
            MMIX_MONO_AUDIO_IN("_7", "7"),
            MMIX_MONO_AUDIO_IN("_8", "8"),
            MMIX_MONO_AUDIO_OUT("_1", "1"),
            MMIX_MONO_AUDIO_OUT("_2", "2"),
            MMIX_MONO_AUDIO_OUT("_3", "3"),
            MMIX_MONO_AUDIO_OUT("_4", "4"),
            MMIX_MONO_AUDIO_OUT("_5", "5"),
            MMIX_MONO_AUDIO_OUT("_6", "6"),
            MMIX_MONO_AUDIO_OUT("_7", "7"),
            MMIX_MONO_AUDIO_OUT("_8", "8"),
            BYPASS,

            MMIX_MONO_INPUT("_1", "1"),
            MMIX_MONO_INPUT("_2", "2"),
            MMIX_MONO_INPUT("_3", "3"),
            MMIX_MONO_INPUT("_4", "4"),
            MMIX_MONO_INPUT("_5", "5"),
            MMIX_MONO_INPUT("_6", "6"),
            MMIX_MONO_INPUT("_7", "7"),
            MMIX_MONO_INPUT("_8", "8"),

            MMIX_MONO_OUTPUT("_1", "1"),
            MMIX_MONO_OUTPUT("_2", "2"),
            MMIX_MONO_OUTPUT("_3", "3"),
            MMIX_MONO_OUTPUT("_4", "4"),
            MMIX_MONO_OUTPUT("_5", "5"),
            MMIX_MONO_OUTPUT("_6", "6"),
            MMIX_MONO_OUTPUT("_7", "7"),
            MMIX_MONO_OUTPUT("_8", "8"),

            MMIX_ROW_X8("_1", "1", GAIN_AMP_0_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB),
            MMIX_ROW_X8("_2", "2", GAIN_AMP_M_INF_DB, GAIN_AMP_0_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB),
            MMIX_ROW_X8("_3", "3", GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_0_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB),
            MMIX_ROW_X8("_4", "4", GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_0_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB),
            MMIX_ROW_X8("_5", "5", GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_0_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB),
            MMIX_ROW_X8("_6", "6", GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_0_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB),
            MMIX_ROW_X8("_7", "7", GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_0_DB, GAIN_AMP_M_INF_DB),
            MMIX_ROW_X8("_8", "8", GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_0_DB),

            PORTS_END
        };

        static const port_t matrix_mixer_x4_stereo_ports[] =
        {
            MMIX_STEREO_AUDIO_IN("_1", "1"),
            MMIX_STEREO_AUDIO_IN("_2", "2"),
            MMIX_STEREO_AUDIO_IN("_3", "3"),
            MMIX_STEREO_AUDIO_IN("_4", "4"),
            MMIX_STEREO_AUDIO_OUT("_1", "1"),
            MMIX_STEREO_AUDIO_OUT("_2", "2"),
            MMIX_STEREO_AUDIO_OUT("_3", "3"),
            MMIX_STEREO_AUDIO_OUT("_4", "4"),
            BYPASS,

            MMIX_STEREO_INPUT("_1", "1"),
            MMIX_STEREO_INPUT("_2", "2"),
            MMIX_STEREO_INPUT("_3", "3"),
            MMIX_STEREO_INPUT("_4", "4"),

            MMIX_STEREO_OUTPUT("_1", "1"),
            MMIX_STEREO_OUTPUT("_2", "2"),
            MMIX_STEREO_OUTPUT("_3", "3"),
            MMIX_STEREO_OUTPUT("_4", "4"),

            MMIX_ROW_X4("_1", "1", GAIN_AMP_0_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB),
            MMIX_ROW_X4("_2", "2", GAIN_AMP_M_INF_DB, GAIN_AMP_0_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB),
            MMIX_ROW_X4("_3", "3", GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_0_DB, GAIN_AMP_M_INF_DB),
            MMIX_ROW_X4("_4", "4", GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_0_DB),

            PORTS_END
        };

        static const port_t matrix_mixer_x8_stereo_ports[] =
        {
            MMIX_STEREO_AUDIO_IN("_1", "1"),
            MMIX_STEREO_AUDIO_IN("_2", "2"),
            MMIX_STEREO_AUDIO_IN("_3", "3"),
            MMIX_STEREO_AUDIO_IN("_4", "4"),
            MMIX_STEREO_AUDIO_IN("_5", "5"),
            MMIX_STEREO_AUDIO_IN("_6", "6"),
            MMIX_STEREO_AUDIO_IN("_7", "7"),
            MMIX_STEREO_AUDIO_IN("_8", "8"),
            MMIX_STEREO_AUDIO_OUT("_1", "1"),
            MMIX_STEREO_AUDIO_OUT("_2", "2"),
            MMIX_STEREO_AUDIO_OUT("_3", "3"),
            MMIX_STEREO_AUDIO_OUT("_4", "4"),
            MMIX_STEREO_AUDIO_OUT("_5", "5"),
            MMIX_STEREO_AUDIO_OUT("_6", "6"),
            MMIX_STEREO_AUDIO_OUT("_7", "7"),
            MMIX_STEREO_AUDIO_OUT("_8", "8"),
            BYPASS,

            MMIX_STEREO_INPUT("_1", "1"),
            MMIX_STEREO_INPUT("_2", "2"),
            MMIX_STEREO_INPUT("_3", "3"),
            MMIX_STEREO_INPUT("_4", "4"),
            MMIX_STEREO_INPUT("_5", "5"),
            MMIX_STEREO_INPUT("_6", "6"),
            MMIX_STEREO_INPUT("_7", "7"),
            MMIX_STEREO_INPUT("_8", "8"),

            MMIX_STEREO_OUTPUT("_1", "1"),
            MMIX_STEREO_OUTPUT("_2", "2"),
            MMIX_STEREO_OUTPUT("_3", "3"),
            MMIX_STEREO_OUTPUT("_4", "4"),
            MMIX_STEREO_OUTPUT("_5", "5"),
            MMIX_STEREO_OUTPUT("_6", "6"),
            MMIX_STEREO_OUTPUT("_7", "7"),
            MMIX_STEREO_OUTPUT("_8", "8"),

            MMIX_ROW_X8("_1", "1", GAIN_AMP_0_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB),
            MMIX_ROW_X8("_2", "2", GAIN_AMP_M_INF_DB, GAIN_AMP_0_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB),
            MMIX_ROW_X8("_3", "3", GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_0_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB),
            MMIX_ROW_X8("_4", "4", GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_0_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB),
            MMIX_ROW_X8("_5", "5", GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_0_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB),
            MMIX_ROW_X8("_6", "6", GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_0_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB),
            MMIX_ROW_X8("_7", "7", GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_0_DB, GAIN_AMP_M_INF_DB),
            MMIX_ROW_X8("_8", "8", GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_M_INF_DB, GAIN_AMP_0_DB),

            PORTS_END
        };

        static const int plugin_classes[]       = { C_MIXER, -1 };
        static const int clap_features_mono[]   = { CF_AUDIO_EFFECT, CF_MIXING, CF_MONO, -1 };
        static const int clap_features_stereo[] = { CF_AUDIO_EFFECT, CF_MIXING, CF_STEREO, -1 };

        const meta::bundle_t matrix_mixer_bundle =
        {
            "matrix_mixer",
            "Matrix Mixer",
            B_UTILITIES,
            "",
            "Routes each input to each output with an individual gain for every crosspoint"
        };

        // Port groups: the first input and the first output are the main ones
        #define MMIX_GROUP_PORTS(i) \
            MONO_PORT_GROUP_PORT(matrix_mixer_pg_mono_in_ ## i, "in_" #i); \
            MONO_PORT_GROUP_PORT(matrix_mixer_pg_mono_out_ ## i, "out_" #i); \
            STEREO_PORT_GROUP_PORTS(matrix_mixer_pg_stereo_in_ ## i, "in_" #i "l", "in_" #i "r"); \
            STEREO_PORT_GROUP_PORTS(matrix_mixer_pg_stereo_out_ ## i, "out_" #i "l", "out_" #i "r");

        #define MMIX_FLAGS(i, f) \
            ((i == 1) ? (f | PGF_MAIN) : f)

        #define MMIX_MONO_IN_GROUP(i) \
            { "matrix_in" #i, "Matrix input " #i,      GRP_MONO,       MMIX_FLAGS(i, PGF_IN),      matrix_mixer_pg_mono_in_ ## i ## _ports     }

        #define MMIX_MONO_OUT_GROUP(i) \
            { "matrix_out" #i, "Matrix output " #i,    GRP_MONO,       MMIX_FLAGS(i, PGF_OUT),     matrix_mixer_pg_mono_out_ ## i ## _ports    }

        #define MMIX_STEREO_IN_GROUP(i) \
            { "matrix_in" #i, "Matrix input " #i,      GRP_STEREO,     MMIX_FLAGS(i, PGF_IN),      matrix_mixer_pg_stereo_in_ ## i ## _ports   }

        #define MMIX_STEREO_OUT_GROUP(i) \
            { "matrix_out" #i, "Matrix output " #i,    GRP_STEREO,     MMIX_FLAGS(i, PGF_OUT),     matrix_mixer_pg_stereo_out_ ## i ## _ports  }

        MMIX_GROUP_PORTS(1)
        MMIX_GROUP_PORTS(2)
        MMIX_GROUP_PORTS(3)
        MMIX_GROUP_PORTS(4)
        MMIX_GROUP_PORTS(5)
        MMIX_GROUP_PORTS(6)
        MMIX_GROUP_PORTS(7)
        MMIX_GROUP_PORTS(8)

        static const port_group_t matrix_mixer_x4_mono_port_groups[] =
        {
            MMIX_MONO_IN_GROUP(1),
            MMIX_MONO_IN_GROUP(2),
            MMIX_MONO_IN_GROUP(3),
            MMIX_MONO_IN_GROUP(4),
            MMIX_MONO_OUT_GROUP(1),
            MMIX_MONO_OUT_GROUP(2),
            MMIX_MONO_OUT_GROUP(3),
            MMIX_MONO_OUT_GROUP(4),
            PORT_GROUPS_END
        };

        static const port_group_t matrix_mixer_x8_mono_port_groups[] =
        {
            MMIX_MONO_IN_GROUP(1),
            MMIX_MONO_IN_GROUP(2),
            MMIX_MONO_IN_GROUP(3),
            MMIX_MONO_IN_GROUP(4),
            MMIX_MONO_IN_GROUP(5),
            MMIX_MONO_IN_GROUP(6),
            MMIX_MONO_IN_GROUP(7),
            MMIX_MONO_IN_GROUP(8),
            MMIX_MONO_OUT_GROUP(1),
            MMIX_MONO_OUT_GROUP(2),
            MMIX_MONO_OUT_GROUP(3),
            MMIX_MONO_OUT_GROUP(4),
            MMIX_MONO_OUT_GROUP(5),
            MMIX_MONO_OUT_GROUP(6),
            MMIX_MONO_OUT_GROUP(7),
            MMIX_MONO_OUT_GROUP(8),
            PORT_GROUPS_END
        };

        static const port_group_t matrix_mixer_x4_stereo_port_groups[] =
        {
            MMIX_STEREO_IN_GROUP(1),
            MMIX_STEREO_IN_GROUP(2),
            MMIX_STEREO_IN_GROUP(3),
            MMIX_STEREO_IN_GROUP(4),
            MMIX_STEREO_OUT_GROUP(1),
            MMIX_STEREO_OUT_GROUP(2),
            MMIX_STEREO_OUT_GROUP(3),
            MMIX_STEREO_OUT_GROUP(4),
            PORT_GROUPS_END
        };

        static const port_group_t matrix_mixer_x8_stereo_port_groups[] =
        {
            MMIX_STEREO_IN_GROUP(1),
            MMIX_STEREO_IN_GROUP(2),
            MMIX_STEREO_IN_GROUP(3),
            MMIX_STEREO_IN_GROUP(4),
            MMIX_STEREO_IN_GROUP(5),
            MMIX_STEREO_IN_GROUP(6),
            MMIX_STEREO_IN_GROUP(7),
            MMIX_STEREO_IN_GROUP(8),
            MMIX_STEREO_OUT_GROUP(1),
            MMIX_STEREO_OUT_GROUP(2),
            MMIX_STEREO_OUT_GROUP(3),
            MMIX_STEREO_OUT_GROUP(4),
            MMIX_STEREO_OUT_GROUP(5),
            MMIX_STEREO_OUT_GROUP(6),
            MMIX_STEREO_OUT_GROUP(7),
            MMIX_STEREO_OUT_GROUP(8),
            PORT_GROUPS_END
        };

        const plugin_t matrix_mixer_x4_mono =
        {
            "Matrixmischer x4 Mono",
            "Matrix Mixer x4 Mono",
            "Matrix Mixer x4 Mono",
            "MM4M",
            &developers::j_ortega,
            "matrix_mixer_x4_mono",
            {
                LSP_LV2_URI("matrix_mixer_x4_mono"),
                LSP_LV2UI_URI("matrix_mixer_x4_mono"),
                "mx4m",
                LSP_VST3_UID("mm4m    mx4m"),
                LSP_VST3UI_UID("mm4m    mx4m"),
                LSP_LADSPA_MATRIX_MIXER_BASE + 0,
                LSP_LADSPA_URI("matrix_mixer_x4_mono"),
                LSP_CLAP_URI("matrix_mixer_x4_mono"),
                LSP_GST_UID("matrix_mixer_x4_mono"),
            },
            LSP_PLUGINS_MATRIX_MIXER_VERSION,
            plugin_classes,
            clap_features_mono,
            E_DUMP_STATE | E_KVT_SYNC,
            matrix_mixer_x4_mono_ports,
            "plugins/util/matrix_mixer/mono.xml",
            NULL,
            matrix_mixer_x4_mono_port_groups,
            &matrix_mixer_bundle,
            3
        };
        LSP_REGISTER_METADATA(matrix_mixer_x4_mono);

        const plugin_t matrix_mixer_x8_mono =
        {
            "Matrixmischer x8 Mono",
            "Matrix Mixer x8 Mono",
            "Matrix Mixer x8 Mono",
            "MM8M",
            &developers::j_ortega,
            "matrix_mixer_x8_mono",
            {
                LSP_LV2_URI("matrix_mixer_x8_mono"),
                LSP_LV2UI_URI("matrix_mixer_x8_mono"),
                "mx8m",
                LSP_VST3_UID("mm8m    mx8m"),
                LSP_VST3UI_UID("mm8m    mx8m"),
                LSP_LADSPA_MATRIX_MIXER_BASE + 1,
                LSP_LADSPA_URI("matrix_mixer_x8_mono"),
                LSP_CLAP_URI("matrix_mixer_x8_mono"),
                LSP_GST_UID("matrix_mixer_x8_mono"),
            },
            LSP_PLUGINS_MATRIX_MIXER_VERSION,
            plugin_classes,
            clap_features_mono,
            E_DUMP_STATE | E_KVT_SYNC,
            matrix_mixer_x8_mono_ports,
            "plugins/util/matrix_mixer/mono.xml",
            NULL,
            matrix_mixer_x8_mono_port_groups,
            &matrix_mixer_bundle,
            4
        };
        LSP_REGISTER_METADATA(matrix_mixer_x8_mono);

        const plugin_t matrix_mixer_x4_stereo =
        {
            "Matrixmischer x4 Stereo",
            "Matrix Mixer x4 Stereo",
            "Matrix Mixer x4 Stereo",
            "MM4S",
            &developers::j_ortega,
            "matrix_mixer_x4_stereo",
            {
                LSP_LV2_URI("matrix_mixer_x4_stereo"),
                LSP_LV2UI_URI("matrix_mixer_x4_stereo"),
                "mx4s",
                LSP_VST3_UID("mm4s    mx4s"),
                LSP_VST3UI_UID("mm4s    mx4s"),
                LSP_LADSPA_MATRIX_MIXER_BASE + 2,
                LSP_LADSPA_URI("matrix_mixer_x4_stereo"),
                LSP_CLAP_URI("matrix_mixer_x4_stereo"),
                LSP_GST_UID("matrix_mixer_x4_stereo"),
            },
            LSP_PLUGINS_MATRIX_MIXER_VERSION,
            plugin_classes,
            clap_features_stereo,
            E_DUMP_STATE | E_KVT_SYNC,
            matrix_mixer_x4_stereo_ports,
            "plugins/util/matrix_mixer/stereo.xml",
            NULL,
            matrix_mixer_x4_stereo_port_groups,
            &matrix_mixer_bundle,
            1
        };
        LSP_REGISTER_METADATA(matrix_mixer_x4_stereo);

        const plugin_t matrix_mixer_x8_stereo =
        {
            "Matrixmischer x8 Stereo",
            "Matrix Mixer x8 Stereo",
            "Matrix Mixer x8 Stereo",
            "MM8S",
            &developers::j_ortega,
            "matrix_mixer_x8_stereo",
            {
                LSP_LV2_URI("matrix_mixer_x8_stereo"),
                LSP_LV2UI_URI("matrix_mixer_x8_stereo"),
                "mx8s",
                LSP_VST3_UID("mm8s    mx8s"),
                LSP_VST3UI_UID("mm8s    mx8s"),
                LSP_LADSPA_MATRIX_MIXER_BASE + 3,
                LSP_LADSPA_URI("matrix_mixer_x8_stereo"),
                LSP_CLAP_URI("matrix_mixer_x8_stereo"),
                LSP_GST_UID("matrix_mixer_x8_stereo"),
            },
            LSP_PLUGINS_MATRIX_MIXER_VERSION,
            plugin_classes,
            clap_features_stereo,
            E_DUMP_STATE | E_KVT_SYNC,
            matrix_mixer_x8_stereo_ports,
            "plugins/util/matrix_mixer/stereo.xml",
            NULL,
            matrix_mixer_x8_stereo_port_groups,
            &matrix_mixer_bundle,
            2
        };
        LSP_REGISTER_METADATA(matrix_mixer_x8_stereo);

    } /* namespace meta */
} /* namespace lsp */
