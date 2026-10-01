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

#include <lsp-plug.in/common/debug.h>
#include <lsp-plug.in/plug-fw/ui.h>
#include <lsp-plug.in/stdlib/stdio.h>
#include <lsp-plug.in/stdlib/stdlib.h>

#include <private/plugins/matrix_mixer.h>
#include <private/ui/matrix_mixer.h>


namespace lsp
{
    namespace plugui
    {
        //---------------------------------------------------------------------
        // Plugin UI factory
        static const meta::plugin_t *plugin_uis[] =
        {
            &meta::matrix_mixer_x4_mono,
            &meta::matrix_mixer_x8_mono,
            &meta::matrix_mixer_x4_stereo,
            &meta::matrix_mixer_x8_stereo
        };

        static ui::Module *ui_factory(const meta::plugin_t *meta)
        {
            return new matrix_mixer(meta);
        }

        static ui::Factory factory(ui_factory, plugin_uis, 4);


        // Two presses on a crosspoint knob closer than this belong to the same double click
        static constexpr ws::ui_timestamp_t DBL_CLICK_TIME     = 600;

        //---------------------------------------------------------------------
        // Plugin UI
        matrix_mixer::matrix_mixer(const meta::plugin_t *meta)
            : ui::Module(meta)
        {
        }

        matrix_mixer::~matrix_mixer()
        {
        }

        status_t matrix_mixer::post_init()
        {
            status_t res = ui::Module::post_init();
            if (res != STATUS_OK)
                return res;

            // Names of the inputs and the outputs
            if ((res = add_channels("input_name", "input", "lists.matrix_mixer.input")) != STATUS_OK)
                return res;
            if ((res = add_channels("output_name", "output", "lists.matrix_mixer.output")) != STATUS_OK)
                return res;

            // Double click on a crosspoint knob toggles the crosspoint
            if ((res = add_cells()) != STATUS_OK)
                return res;

            return STATUS_OK;
        }

        status_t matrix_mixer::add_channels(const char *widget, const char *prefix, const char *dfl)
        {
            for (size_t i=0; ; ++i)
            {
                // Each input has a gain port "cg_N", each output a gain port "og_N"
                ui::IPort *gain = find_port((prefix[0] == 'i') ? "cg" : "og", i+1);
                if (gain == NULL)
                    break;

                channel_t *c = vChannels.add();
                if (c == NULL)
                    return STATUS_NO_MEM;

                c->nIndex       = i+1;
                c->sPrefix      = prefix;
                c->sDefault     = dfl;
                c->bNameChanged = false;
                c->wName        = find_widget<tk::Edit>(widget, c->nIndex);

                if (c->wName != NULL)
                {
                    c->wName->text()->set(dfl);
                    c->wName->text()->params()->set_int("id", int(c->nIndex));

                    c->wName->slots()->bind(tk::SLOT_CHANGE, slot_channel_name_updated, c);
                }
            }

            return STATUS_OK;
        }

        status_t matrix_mixer::add_cells()
        {
            // Count the channels by the input gain ports
            size_t channels = 0;
            while (find_port("cg", channels + 1) != NULL)
                ++channels;

            // Reserve the memory first: the slots keep pointers to the cells
            if (!vCells.reserve(channels * channels))
                return STATUS_NO_MEM;

            LSPString uid;
            for (size_t i=1; i<=channels; ++i)
                for (size_t o=1; o<=channels; ++o)
                {
                    cell_t *c       = vCells.add();
                    if (c == NULL)
                        return STATUS_NO_MEM;

                    c->pUI          = this;
                    uid.fmt_ascii("mx_%d_%d", int(i), int(o));
                    c->pGain        = pWrapper->port(&uid);
                    uid.fmt_ascii("cell_%d_%d", int(i), int(o));
                    c->wKnob        = pWrapper->controller()->widgets()->get<tk::Knob>(&uid);
                    c->fLast        = GAIN_AMP_0_DB;
                    c->fBefore      = GAIN_AMP_M_INF_DB;
                    c->nLastDown    = 0;

                    if ((c->pGain != NULL) && (c->wKnob != NULL))
                    {
                        c->wKnob->slots()->bind(tk::SLOT_MOUSE_DOWN, slot_cell_mouse_down, c);
                        c->wKnob->slots()->bind(tk::SLOT_MOUSE_DBL_CLICK, slot_cell_dbl_click, c);
                    }
                }

            return STATUS_OK;
        }

        void matrix_mixer::toggle_cell(cell_t *c)
        {
            const meta::port_t *meta    = c->pGain->metadata();
            const float min             = (meta != NULL) ? meta->min : GAIN_AMP_M_INF_DB;
            // The first click of the double click may already have moved the knob
            // (a click on the scale sets the value): toggle the value from before it.
            const float value           = c->fBefore;

            // Audible crosspoint: remember the gain and mute it.
            // Muted crosspoint: restore the last audible gain (0 dB by default).
            float next;
            if (value > min)
            {
                c->fLast        = value;
                next            = min;
            }
            else
                next            = (c->fLast > min) ? c->fLast : GAIN_AMP_0_DB;

            c->pGain->set_value(next);
            c->pGain->notify_all(ui::PORT_USER_EDIT);
        }

        status_t matrix_mixer::slot_cell_mouse_down(tk::Widget *sender, void *ptr, void *data)
        {
            cell_t *c           = static_cast<cell_t *>(ptr);
            const ws::event_t *ev = static_cast<const ws::event_t *>(data);
            if ((c == NULL) || (ev == NULL))
                return STATUS_OK;

            // A press long after the previous one starts a new (possible) double click
            if ((ev->nTime - c->nLastDown) > DBL_CLICK_TIME)
                c->fBefore      = c->pGain->value();
            c->nLastDown        = ev->nTime;

            return STATUS_OK;
        }

        status_t matrix_mixer::slot_cell_dbl_click(tk::Widget *sender, void *ptr, void *data)
        {
            cell_t *c = static_cast<cell_t *>(ptr);
            if ((c != NULL) && (c->pUI != NULL))
                c->pUI->toggle_cell(c);
            return STATUS_OK;
        }

        ui::IPort *matrix_mixer::find_port(const char *prefix, size_t id)
        {
            LSPString uid;
            uid.fmt_ascii("%s_%d", prefix, int(id));
            return pWrapper->port(&uid);
        }

        template <class T>
        T *matrix_mixer::find_widget(const char *prefix, size_t id)
        {
            LSPString uid;
            uid.fmt_ascii("%s_%d", prefix, int(id));
            return pWrapper->controller()->widgets()->get<T>(&uid);
        }

        void matrix_mixer::notify(ui::IPort *port, size_t flags)
        {
        }

        void matrix_mixer::sync_channel_names(core::KVTStorage *kvt)
        {
            LSPString value;

            for (size_t i=0, n=vChannels.size(); i<n; ++i)
            {
                channel_t *c = vChannels.uget(i);
                if ((c->wName == NULL) || (!c->bNameChanged))
                    continue;

                // Obtain the new channel name
                if (c->wName->text()->format(&value) != STATUS_OK)
                    continue;

                // Submit new value to KVT
                set_channel_name(kvt, c, value.get_utf8());
                c->bNameChanged = false;
            }
        }

        void matrix_mixer::set_channel_name(core::KVTStorage *kvt, const channel_t *c, const char *name)
        {
            char kvt_name[0x80];
            core::kvt_param_t kparam;

            // Submit new value to KVT
            snprintf(kvt_name, sizeof(kvt_name), "/%s/%d/name", c->sPrefix, int(c->nIndex));
            kparam.type     = core::KVT_STRING;
            kparam.str      = name;
            lsp_trace("%s = %s", kvt_name, kparam.str);
            kvt->put(kvt_name, &kparam, core::KVT_TO_DSP);
            wrapper()->kvt_notify_write(kvt, kvt_name, &kparam);
        }

        void matrix_mixer::idle()
        {
            // Scan the list of channel names for changes
            size_t changes = 0;
            for (size_t i=0, n=vChannels.size(); i<n; ++i)
            {
                channel_t *c = vChannels.uget(i);
                if ((c->wName != NULL) && (c->bNameChanged))
                    ++changes;
            }

            // Apply channel names to KVT
            if (changes > 0)
            {
                core::KVTStorage *kvt = wrapper()->kvt_lock();
                if (kvt != NULL)
                {
                    sync_channel_names(kvt);
                    wrapper()->kvt_release();
                }
            }
        }

        void matrix_mixer::kvt_changed(core::KVTStorage *kvt, const char *id, const core::kvt_param_t *value)
        {
            ui::Module::kvt_changed(kvt, id, value);

            if (value->type != core::KVT_STRING)
                return;

            for (size_t i=0, n=vChannels.size(); i<n; ++i)
            {
                channel_t *c = vChannels.uget(i);
                if (c->wName == NULL)
                    continue;

                // Match "/<prefix>/<index>/name"
                char kvt_name[0x80];
                snprintf(kvt_name, sizeof(kvt_name), "/%s/%d/name", c->sPrefix, int(c->nIndex));
                if (::strcmp(id, kvt_name) != 0)
                    continue;

                c->wName->text()->set_raw(value->str);
                c->bNameChanged = false;
            }
        }

        status_t matrix_mixer::reset_settings()
        {
            core::KVTStorage *kvt = wrapper()->kvt_lock();
            if (kvt != NULL)
            {
                // Reset names of all inputs and outputs
                for (size_t i=0, n=vChannels.size(); i<n; ++i)
                {
                    channel_t *c = vChannels.uget(i);
                    if (c->wName == NULL)
                        continue;

                    c->wName->text()->set(c->sDefault);
                    c->wName->text()->params()->set_int("id", int(c->nIndex));
                    c->bNameChanged  = true;
                }

                sync_channel_names(kvt);
                wrapper()->kvt_release();
            }

            return ui::Module::reset_settings();
        }

        status_t matrix_mixer::slot_channel_name_updated(tk::Widget *sender, void *ptr, void *data)
        {
            channel_t *c    = static_cast<channel_t *>(ptr);
            c->bNameChanged = true;

            return STATUS_OK;
        }

    } /* namespace plugui */
} /* namespace lsp */
