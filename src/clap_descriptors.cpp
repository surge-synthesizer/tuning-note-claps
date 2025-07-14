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
#include <cstring>

#include <clap/factory/plugin-factory.h>
#include <clap/entry.h>

#include "clap_creators.h"

#include "cmake_info.h"

const char *MTSNE_features[] = {CLAP_PLUGIN_FEATURE_NOTE_EFFECT, "microtonal", "MTS-ESP", nullptr};
clap_plugin_descriptor MTSNE_desc = {
    CLAP_VERSION,
    "org.surge-synth-team.MTSToNoteExpression",
    "MTS-ESP To Note Expression",
    "Surge Synth Team",
    "https://surge-synth-team.org",
    "",
    "",
    getProjectVersion(),
    "Augment a note stream with Pitch Note Expressions to retune using MTS-ESP",
    MTSNE_features};

const char *EDNM_features[] = {CLAP_PLUGIN_FEATURE_NOTE_EFFECT, "microtonal", "even-division",
                               nullptr};
clap_plugin_descriptor EDNM_desc = {
    CLAP_VERSION,
    "org.surge-synth-team.EDNMToNoteExpression",
    "EDN-M To Note Expression",
    "Surge Synth Team",
    "https://surge-synth-team.org",
    "",
    "",
    getProjectVersion(),
    "Augment a note stream with Pitch Note Expressions to retune using a single EDN-M scale",
    EDNM_features};

uint32_t mtsne_get_plugin_count(const struct clap_plugin_factory *) { return 2; }
const clap_plugin_descriptor *mtsne_get_plugin_descriptor(const struct clap_plugin_factory *,
                                                          uint32_t idx)
{

    switch (idx)
    {
    case 0:
        return &MTSNE_desc;
    case 1:
        return &EDNM_desc;
    }
    return nullptr;
}

const clap_plugin *mtsne_create_plugin(const struct clap_plugin_factory *, const clap_host *host,
                                       const char *plugin_id)
{
    if (strcmp(plugin_id, MTSNE_desc.id) == 0)
        return create_mtsne(&MTSNE_desc, host);

    if (strcmp(plugin_id, EDNM_desc.id) == 0)
        return create_ednmne(&EDNM_desc, host);

    return nullptr;
}

const struct clap_plugin_factory mtsne_clap_plugin_factory = {
    mtsne_get_plugin_count,
    mtsne_get_plugin_descriptor,
    mtsne_create_plugin,
};

bool mtsne_clap_init(const char *) { return true; }
void mtsne_clap_deinit(void) {}
const void *mtsne_clap_get_factory(const char *factory_id)
{
    if (strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0)
    {
        return &mtsne_clap_plugin_factory;
    }

    return nullptr;
}

extern "C"
{
    const CLAP_EXPORT struct clap_plugin_entry clap_entry = {
        CLAP_VERSION, mtsne_clap_init, mtsne_clap_deinit, mtsne_clap_get_factory};
}