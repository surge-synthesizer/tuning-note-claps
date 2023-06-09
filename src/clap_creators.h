/*
 * tuning-note-claps
 * https://github.com/surge-synthesizer/tuning-note-claps
 *
 * Released under the MIT License, included in the file "LICENSE.md"
 * Copyright 2022, Paul Walker and other contributors as listed in the github
 * transaction log.
 *
 * tuning-note-claps provides a set of CLAP plugins which augment
 * note expression streams with Note Expressions for microtonal features.
 * It is free and open source software.
 */

#ifndef MTSTONOTEEXPRESSION_CLAP_CREATORS_H
#define MTSTONOTEEXPRESSION_CLAP_CREATORS_H

#include <clap/plugin.h>
#include <clap/host.h>

extern const clap_plugin *create_mtsne(const clap_plugin_descriptor_t *desc, const clap_host *host);
extern const clap_plugin *create_ednmne(const clap_plugin_descriptor_t *desc,
                                        const clap_host *host);

#endif // MTSTONOTEEXPRESSION_CLAP_CREATORS_H
