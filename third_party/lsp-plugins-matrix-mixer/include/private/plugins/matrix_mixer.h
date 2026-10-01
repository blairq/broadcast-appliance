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

#ifndef PRIVATE_PLUGINS_MATRIX_MIXER_H_
#define PRIVATE_PLUGINS_MATRIX_MIXER_H_

#include <lsp-plug.in/dsp-units/ctl/Bypass.h>
#include <lsp-plug.in/plug-fw/plug.h>
#include <private/meta/matrix_mixer.h>

namespace lsp
{
    namespace plugins
    {
        /**
         * Matrix mixer: routes each of N inputs to each of N outputs with
         * an individual gain for every crosspoint. Inputs have solo, mute,
         * phase and gain controls; outputs have mute and gain controls.
         */
        class matrix_mixer: public plug::Module
        {
            private:
                matrix_mixer & operator = (const matrix_mixer &);
                matrix_mixer (const matrix_mixer &);

            protected:
                typedef struct input_t
                {
                    float              *vIn[2];         // Input buffer pointers
                    float              *vBuf[2];        // Processed (pre-matrix) signal
                    float               fOldGain;       // Old input gain
                    float               fGain;          // Input gain (including mute, solo and phase)
                    bool                bSolo;          // Solo flag

                    plug::IPort        *pIn[2];         // Input data ports
                    plug::IPort        *pSolo;          // Solo switch
                    plug::IPort        *pMute;          // Mute switch
                    plug::IPort        *pPhase;         // Phase invert switch
                    plug::IPort        *pGain;          // Input gain
                    plug::IPort        *pLevel[2];      // Input level meters
                } input_t;

                typedef struct output_t
                {
                    dspu::Bypass        sBypass[2];     // Bypass switches

                    float              *vOut[2];        // Output buffer pointers
                    float              *vBuf[2];        // Mixed signal
                    float               fOldGain[2];    // Old output gain (balanced)
                    float               fGain[2];       // Output gain (balanced, including mute)

                    plug::IPort        *pOut[2];        // Output data ports
                    plug::IPort        *pMute;          // Mute switch
                    plug::IPort        *pGain;          // Output gain
                    plug::IPort        *pBalance;       // Output balance (stereo only)
                    plug::IPort        *pLevel[2];      // Output level meters
                } output_t;

                typedef struct cell_t
                {
                    float               fOldGain;       // Old crosspoint gain
                    float               fGain;          // Crosspoint gain
                    plug::IPort        *pGain;          // Crosspoint gain port
                } cell_t;

            protected:
                input_t            *vInputs;            // Inputs
                output_t           *vOutputs;           // Outputs
                cell_t             *vCells;             // Crosspoints: nChannels x nChannels, row = input
                size_t              nChannels;          // Number of inputs (and outputs)
                size_t              nSides;             // Number of audio channels per input/output (1 or 2)

                plug::IPort        *pBypass;            // Bypass switch

                uint8_t            *pData;              // Allocated data

            protected:
                inline cell_t      *cell(size_t in, size_t out) { return &vCells[in * nChannels + out]; }
                void                do_destroy();

            public:
                explicit matrix_mixer(const meta::plugin_t *meta);
                virtual ~matrix_mixer() override;

                virtual void        init(plug::IWrapper *wrapper, plug::IPort **ports) override;
                virtual void        destroy() override;

            public:
                virtual void        update_sample_rate(long sr) override;
                virtual void        update_settings() override;
                virtual void        process(size_t samples) override;
                virtual void        dump(dspu::IStateDumper *v) const override;
        };
    } /* namespace plugins */
} /* namespace lsp */

#endif /* PRIVATE_PLUGINS_MATRIX_MIXER_H_ */
