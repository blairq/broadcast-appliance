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

#ifndef PRIVATE_UI_MATRIX_MIXER_H_
#define PRIVATE_UI_MATRIX_MIXER_H_

#include <lsp-plug.in/plug-fw/ui.h>
#include <lsp-plug.in/lltl/darray.h>

namespace lsp
{
    namespace plugui
    {
        /**
         * Matrix Mixer plugin series UI: keeps the user-defined names of the
         * inputs and outputs in the KVT storage
         */
        class matrix_mixer: public ui::Module, public ui::IPortListener
        {
            protected:
                typedef struct channel_t
                {
                    tk::Edit                   *wName;          // Edit that holds channel name
                    const char                 *sPrefix;        // KVT prefix: "input" or "output"
                    const char                 *sDefault;       // Default name from the dictionary

                    size_t                      nIndex;         // Channel index
                    bool                        bNameChanged;   // Indicator that channel name has changed
                } channel_t;

            protected:
                lltl::darray<channel_t>     vChannels;          // List of named inputs and outputs

            protected:
                ui::IPort          *find_port(const char *prefix, size_t id);

                template <class T>
                T                  *find_widget(const char *prefix, size_t id);

                status_t            add_channels(const char *widget, const char *prefix, const char *dfl);
                void                sync_channel_names(core::KVTStorage *kvt);
                void                set_channel_name(core::KVTStorage *kvt, const channel_t *c, const char *name);

            protected:
                static status_t     slot_channel_name_updated(tk::Widget *sender, void *ptr, void *data);

            public:
                explicit matrix_mixer(const meta::plugin_t *meta);
                virtual ~matrix_mixer() override;

            public:
                virtual status_t    post_init() override;
                virtual void        notify(ui::IPort *port, size_t flags) override;
                virtual void        idle() override;
                virtual void        kvt_changed(core::KVTStorage *kvt, const char *id, const core::kvt_param_t *value) override;
                virtual status_t    reset_settings() override;
        };

    } /* namespace plugui */
} /* namespace lsp */


#endif /* PRIVATE_UI_MATRIX_MIXER_H_ */
