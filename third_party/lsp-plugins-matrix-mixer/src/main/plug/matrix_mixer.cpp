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

#include <lsp-plug.in/common/alloc.h>
#include <lsp-plug.in/common/debug.h>
#include <lsp-plug.in/dsp/dsp.h>
#include <lsp-plug.in/dsp-units/units.h>
#include <lsp-plug.in/plug-fw/meta/func.h>
#include <lsp-plug.in/plug-fw/meta/ports.h>
#include <lsp-plug.in/stdlib/string.h>

#include <private/plugins/matrix_mixer.h>
#include <lsp-plug.in/shared/debug.h>

namespace lsp
{
    /* The size of temporary buffer for audio processing */
    static constexpr size_t BUFFER_SIZE     = 0x1000U;

    namespace plugins
    {
        //---------------------------------------------------------------------
        // Plugin factory
        static const meta::plugin_t *plugins[] =
        {
            &meta::matrix_mixer_x4_mono,
            &meta::matrix_mixer_x8_mono,
            &meta::matrix_mixer_x4_stereo,
            &meta::matrix_mixer_x8_stereo
        };

        static plug::Module *plugin_factory(const meta::plugin_t *meta)
        {
            return new matrix_mixer(meta);
        }

        static plug::Factory factory(plugin_factory, plugins, 4);

        //---------------------------------------------------------------------
        // Implementation
        matrix_mixer::matrix_mixer(const meta::plugin_t *meta):
            Module(meta)
        {
            // Compute the number of inputs by the number of audio input ports
            size_t in_ports     = 0;
            size_t sides        = 1;

            for (const meta::port_t *p = meta->ports; p->id != NULL; ++p)
            {
                if (!meta::is_audio_in_port(p))
                    continue;
                ++in_ports;
                if (strcmp(p->id, "in_1l") == 0)
                    sides               = 2;
            }

            vInputs         = NULL;
            vOutputs        = NULL;
            vCells          = NULL;
            nChannels       = in_ports / sides;
            nSides          = sides;

            pBypass         = NULL;

            pData           = NULL;
        }

        matrix_mixer::~matrix_mixer()
        {
            do_destroy();
        }

        void matrix_mixer::init(plug::IWrapper *wrapper, plug::IPort **ports)
        {
            // Call parent class for initialization
            Module::init(wrapper, ports);

            // Estimate the number of bytes to allocate
            size_t szof_inputs      = align_size(sizeof(input_t) * nChannels, DEFAULT_ALIGN);
            size_t szof_outputs     = align_size(sizeof(output_t) * nChannels, DEFAULT_ALIGN);
            size_t szof_cells       = align_size(sizeof(cell_t) * nChannels * nChannels, DEFAULT_ALIGN);
            size_t szof_buf         = align_size(BUFFER_SIZE * sizeof(float), DEFAULT_ALIGN);
            size_t alloc            = szof_inputs + szof_outputs + szof_cells + szof_buf * nSides * nChannels * 2;

            // Allocate memory-aligned data
            uint8_t *ptr            = alloc_aligned<uint8_t>(pData, alloc, DEFAULT_ALIGN);
            if (ptr == NULL)
                return;

            // Initialize pointers
            vInputs                 = advance_ptr_bytes<input_t>(ptr, szof_inputs);
            vOutputs                = advance_ptr_bytes<output_t>(ptr, szof_outputs);
            vCells                  = advance_ptr_bytes<cell_t>(ptr, szof_cells);

            // Initialize inputs
            for (size_t i=0; i<nChannels; ++i)
            {
                input_t *c      = &vInputs[i];

                for (size_t j=0; j<2; ++j)
                {
                    c->vIn[j]       = NULL;
                    c->vBuf[j]      = (j < nSides) ? advance_ptr_bytes<float>(ptr, szof_buf) : NULL;
                    c->pIn[j]       = NULL;
                    c->pLevel[j]    = NULL;
                }
                c->fOldGain     = GAIN_AMP_0_DB;
                c->fGain        = GAIN_AMP_0_DB;
                c->bSolo        = false;

                c->pSolo        = NULL;
                c->pMute        = NULL;
                c->pPhase       = NULL;
                c->pGain        = NULL;
            }

            // Initialize outputs
            for (size_t i=0; i<nChannels; ++i)
            {
                output_t *c     = &vOutputs[i];

                for (size_t j=0; j<2; ++j)
                {
                    c->sBypass[j].construct();

                    c->vOut[j]      = NULL;
                    c->vBuf[j]      = (j < nSides) ? advance_ptr_bytes<float>(ptr, szof_buf) : NULL;
                    c->fOldGain[j]  = GAIN_AMP_0_DB;
                    c->fGain[j]     = GAIN_AMP_0_DB;
                    c->pOut[j]      = NULL;
                    c->pLevel[j]    = NULL;
                }

                c->pMute        = NULL;
                c->pGain        = NULL;
                c->pBalance     = NULL;
            }

            // Initialize crosspoints
            for (size_t i=0; i<nChannels * nChannels; ++i)
            {
                cell_t *c       = &vCells[i];

                c->fOldGain     = GAIN_AMP_M_INF_DB;
                c->fGain        = GAIN_AMP_M_INF_DB;
                c->pGain        = NULL;
            }

            // Bind ports
            lsp_trace("Binding ports");
            size_t port_id      = 0;

            // Bind audio inputs and outputs
            for (size_t i=0; i<nChannels; ++i)
                for (size_t j=0; j<nSides; ++j)
                    BIND_PORT(vInputs[i].pIn[j]);
            for (size_t i=0; i<nChannels; ++i)
                for (size_t j=0; j<nSides; ++j)
                    BIND_PORT(vOutputs[i].pOut[j]);

            BIND_PORT(pBypass);

            // Bind input strips
            for (size_t i=0; i<nChannels; ++i)
            {
                input_t *c      = &vInputs[i];

                BIND_PORT(c->pSolo);
                BIND_PORT(c->pMute);
                BIND_PORT(c->pPhase);
                BIND_PORT(c->pGain);
                for (size_t j=0; j<nSides; ++j)
                    BIND_PORT(c->pLevel[j]);
            }

            // Bind output strips
            for (size_t i=0; i<nChannels; ++i)
            {
                output_t *c     = &vOutputs[i];

                BIND_PORT(c->pMute);
                if (nSides > 1)
                    BIND_PORT(c->pBalance);
                BIND_PORT(c->pGain);
                for (size_t j=0; j<nSides; ++j)
                    BIND_PORT(c->pLevel[j]);
            }

            // Bind crosspoints (row = input, column = output)
            for (size_t i=0; i<nChannels; ++i)
                for (size_t j=0; j<nChannels; ++j)
                    BIND_PORT(cell(i, j)->pGain);
        }

        void matrix_mixer::destroy()
        {
            Module::destroy();
            do_destroy();
        }

        void matrix_mixer::do_destroy()
        {
            vInputs         = NULL;
            vOutputs        = NULL;
            vCells          = NULL;

            // Free previously allocated data chunk
            if (pData != NULL)
            {
                free_aligned(pData);
                pData       = NULL;
            }
        }

        void matrix_mixer::update_sample_rate(long sr)
        {
            for (size_t i=0; i<nChannels; ++i)
            {
                output_t *c     = &vOutputs[i];
                for (size_t j=0; j<nSides; ++j)
                    c->sBypass[j].init(sr);
            }
        }

        void matrix_mixer::update_settings()
        {
            bool bypass     = pBypass->value() >= 0.5f;

            // Check soloing option
            bool has_solo   = false;
            for (size_t i=0; i<nChannels; ++i)
            {
                input_t *c      = &vInputs[i];
                c->bSolo        = c->pSolo->value() >= 0.5f;
                if (c->bSolo)
                    has_solo        = true;
            }

            // Update inputs
            for (size_t i=0; i<nChannels; ++i)
            {
                input_t *c      = &vInputs[i];

                bool mute       = (c->pMute->value() >= 0.5f) || ((has_solo) && (!c->bSolo));
                float gain      = (mute) ? GAIN_AMP_M_INF_DB : c->pGain->value();
                if (c->pPhase->value() >= 0.5f)
                    gain            = -gain;

                c->fOldGain     = c->fGain;
                c->fGain        = gain;
            }

            // Update outputs
            for (size_t i=0; i<nChannels; ++i)
            {
                output_t *c     = &vOutputs[i];

                float gain      = (c->pMute->value() >= 0.5f) ? GAIN_AMP_M_INF_DB : c->pGain->value();

                for (size_t j=0; j<nSides; ++j)
                {
                    c->sBypass[j].set_bypass(bypass);
                    c->fOldGain[j]  = c->fGain[j];
                }

                if (nSides > 1)
                {
                    float balance   = c->pBalance->value() * 0.01f;
                    c->fGain[0]     = gain * (1.0f - balance);
                    c->fGain[1]     = gain * (1.0f + balance);
                }
                else
                    c->fGain[0]     = gain;
            }

            // Update crosspoints
            for (size_t i=0; i<nChannels * nChannels; ++i)
            {
                cell_t *c       = &vCells[i];
                c->fOldGain     = c->fGain;
                c->fGain        = c->pGain->value();
            }
        }

        void matrix_mixer::process(size_t samples)
        {
            // Obtain audio buffers
            for (size_t i=0; i<nChannels; ++i)
            {
                input_t *in     = &vInputs[i];
                output_t *out   = &vOutputs[i];

                for (size_t j=0; j<nSides; ++j)
                {
                    in->vIn[j]      = in->pIn[j]->buffer<float>();
                    out->vOut[j]    = out->pOut[j]->buffer<float>();
                }
            }

            // Main processing
            while (samples > 0)
            {
                size_t to_process           = lsp_min(samples, BUFFER_SIZE);

                // Prepare all inputs first: the host may process in-place, so
                // no output is written until every input has been read
                for (size_t i=0; i<nChannels; ++i)
                {
                    input_t *c      = &vInputs[i];

                    for (size_t j=0; j<nSides; ++j)
                    {
                        dsp::lramp2(c->vBuf[j], c->vIn[j], c->fOldGain, c->fGain, to_process);
                        c->pLevel[j]->set_value(dsp::abs_max(c->vBuf[j], to_process));
                    }
                }

                // Mix each output from all inputs through the matrix
                for (size_t i=0; i<nChannels; ++i)
                {
                    output_t *c     = &vOutputs[i];

                    for (size_t j=0; j<nSides; ++j)
                        dsp::fill_zero(c->vBuf[j], to_process);

                    for (size_t k=0; k<nChannels; ++k)
                    {
                        cell_t *x       = cell(k, i);

                        // Skip silent crosspoints
                        if ((x->fOldGain <= GAIN_AMP_M_INF_DB) && (x->fGain <= GAIN_AMP_M_INF_DB))
                            continue;

                        input_t *in     = &vInputs[k];
                        for (size_t j=0; j<nSides; ++j)
                            dsp::lramp_add2(c->vBuf[j], in->vBuf[j], x->fOldGain, x->fGain, to_process);
                    }

                    // Apply output gain and balance, perform metering and bypass
                    for (size_t j=0; j<nSides; ++j)
                    {
                        dsp::lramp2(c->vBuf[j], c->vBuf[j], c->fOldGain[j], c->fGain[j], to_process);
                        c->pLevel[j]->set_value(dsp::abs_max(c->vBuf[j], to_process));

                        // When bypassed, each output passes the input with the same number
                        c->sBypass[j].process(c->vOut[j], vInputs[i].vIn[j], c->vBuf[j], to_process);

                        c->fOldGain[j]  = c->fGain[j];
                    }
                }

                // Renew old parameters
                for (size_t i=0; i<nChannels; ++i)
                    vInputs[i].fOldGain     = vInputs[i].fGain;
                for (size_t i=0; i<nChannels * nChannels; ++i)
                    vCells[i].fOldGain      = vCells[i].fGain;

                // Update counters and pointers
                samples                    -= to_process;
                for (size_t i=0; i<nChannels; ++i)
                {
                    for (size_t j=0; j<nSides; ++j)
                    {
                        vInputs[i].vIn[j]      += to_process;
                        vOutputs[i].vOut[j]    += to_process;
                    }
                }
            }
        }

        void matrix_mixer::dump(dspu::IStateDumper *v) const
        {
            v->begin_array("vInputs", vInputs, nChannels);
            for (size_t i=0; i<nChannels; ++i)
            {
                const input_t *c = &vInputs[i];

                v->begin_object(c, sizeof(input_t));
                {
                    v->writev("vIn", c->vIn, 2);
                    v->writev("vBuf", c->vBuf, 2);
                    v->write("fOldGain", c->fOldGain);
                    v->write("fGain", c->fGain);
                    v->write("bSolo", c->bSolo);

                    v->writev("pIn", c->pIn, 2);
                    v->write("pSolo", c->pSolo);
                    v->write("pMute", c->pMute);
                    v->write("pPhase", c->pPhase);
                    v->write("pGain", c->pGain);
                    v->writev("pLevel", c->pLevel, 2);
                }
                v->end_object();
            }
            v->end_array();

            v->begin_array("vOutputs", vOutputs, nChannels);
            for (size_t i=0; i<nChannels; ++i)
            {
                const output_t *c = &vOutputs[i];

                v->begin_object(c, sizeof(output_t));
                {
                    v->write_object_array("sBypass", c->sBypass, 2);
                    v->writev("vOut", c->vOut, 2);
                    v->writev("vBuf", c->vBuf, 2);
                    v->writev("fOldGain", c->fOldGain, 2);
                    v->writev("fGain", c->fGain, 2);

                    v->writev("pOut", c->pOut, 2);
                    v->write("pMute", c->pMute);
                    v->write("pGain", c->pGain);
                    v->write("pBalance", c->pBalance);
                    v->writev("pLevel", c->pLevel, 2);
                }
                v->end_object();
            }
            v->end_array();

            v->begin_array("vCells", vCells, nChannels * nChannels);
            for (size_t i=0; i<nChannels * nChannels; ++i)
            {
                const cell_t *c = &vCells[i];

                v->begin_object(c, sizeof(cell_t));
                {
                    v->write("fOldGain", c->fOldGain);
                    v->write("fGain", c->fGain);
                    v->write("pGain", c->pGain);
                }
                v->end_object();
            }
            v->end_array();

            v->write("nChannels", nChannels);
            v->write("nSides", nSides);
            v->write("pBypass", pBypass);
            v->write("pData", pData);
        }

    } /* namespace plugins */
} /* namespace lsp */
