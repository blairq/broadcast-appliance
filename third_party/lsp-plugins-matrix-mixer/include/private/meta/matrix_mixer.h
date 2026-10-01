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

#ifndef PRIVATE_META_MATRIX_MIXER_H_
#define PRIVATE_META_MATRIX_MIXER_H_

#include <lsp-plug.in/plug-fw/meta/types.h>
#include <lsp-plug.in/plug-fw/const.h>
#include <lsp-plug.in/dsp-units/const.h>

namespace lsp
{
    //-------------------------------------------------------------------------
    // Plugin metadata
    namespace meta
    {
        typedef struct matrix_mixer
        {
            static constexpr float  CHANNEL_GAIN_MIN    = GAIN_AMP_M_INF_DB;
            static constexpr float  CHANNEL_GAIN_MAX    = GAIN_AMP_P_12_DB;
            static constexpr float  CHANNEL_GAIN_DFL    = GAIN_AMP_0_DB;
            static constexpr float  CHANNEL_GAIN_STEP   = 0.01f;

            static constexpr float  CELL_GAIN_MIN       = GAIN_AMP_M_INF_DB;
            static constexpr float  CELL_GAIN_MAX       = GAIN_AMP_P_12_DB;
            static constexpr float  CELL_GAIN_DFL       = GAIN_AMP_M_INF_DB;
            static constexpr float  CELL_GAIN_STEP      = 0.01f;
        } matrix_mixer;

        // Plugin type metadata
        extern const plugin_t matrix_mixer_x4_mono;
        extern const plugin_t matrix_mixer_x8_mono;
        extern const plugin_t matrix_mixer_x4_stereo;
        extern const plugin_t matrix_mixer_x8_stereo;
    } /* namespace meta */
} /* namespace lsp */

#endif /* PRIVATE_META_MATRIX_MIXER_H_ */
