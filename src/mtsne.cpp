#include <clap/clap.h>
#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>
#include <clap/helpers/host-proxy.hh>
#include <clap/helpers/host-proxy.hxx>

#include <iostream>
#include <array>

#define _DBGCOUT std::cout << __FILE__ << ":" << __LINE__ << " | "

struct MTSNE : public clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                            clap::helpers::CheckingLevel::Minimal>
{
    static clap_plugin_descriptor desc;
    MTSNE(const clap_host *host)
        : clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                clap::helpers::CheckingLevel::Minimal>(&desc, host)
    {
        for (auto &v : notesOn)
            v = false;
    }

  protected:
    bool implementsNotePorts() const noexcept override { return true; }
    uint32_t notePortsCount(bool isInput) const noexcept override { return 1; }
    bool notePortsInfo(uint32_t index, bool isInput,
                       clap_note_port_info *info) const noexcept override
    {
        info->id = 1 + (isInput ? 1 : 0);
        info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI |
                                   CLAP_NOTE_DIALECT_MIDI_MPE | CLAP_NOTE_DIALECT_MIDI2;
        info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
        if (isInput)
        {
            strncpy(info->name, "MTS Note Input", CLAP_NAME_SIZE);
        }
        else
        {
            strncpy(info->name, "MTS Note Output", CLAP_NAME_SIZE);
        }
    }

    std::array<bool, 127> notesOn;

    clap_process_status process(const clap_process *process) noexcept override
    {
        auto ev = process->in_events;
        auto ov = process->out_events;
        auto sz = ev->size(ev);
        for (uint32_t i = 0; i < sz; ++i)
        {
            auto evt = ev->get(ev, i);
            switch (evt->type)
            {
            case CLAP_EVENT_MIDI:
            case CLAP_EVENT_MIDI2:
            {
                ov->try_push(ov, evt);
            }
            break;
            case CLAP_EVENT_NOTE_ON:
            {
                auto nevt = reinterpret_cast<const clap_event_note *>(evt);
                notesOn[nevt->key] = true;

                auto q = clap_event_note_expression();
                q.header.size = sizeof(clap_event_note_expression);
                q.header.type = (uint16_t)CLAP_EVENT_NOTE_EXPRESSION;
                q.header.time = nevt->header.time;
                q.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                q.header.flags = 0;
                q.key = nevt->key;
                q.channel = nevt->channel;
                q.port_index = nevt->port_index;
                q.expression_id = CLAP_NOTE_EXPRESSION_TUNING;

                auto dFrom60 = nevt->key - 60;
                auto retune = dFrom60 * 12 / 18.0 - dFrom60;

                q.value = retune;

                ov->try_push(ov, evt);
                ov->try_push(ov, reinterpret_cast<const clap_event_header *>(&q));
            }
            break;
            case CLAP_EVENT_NOTE_OFF:
            {
                auto nevt = reinterpret_cast<const clap_event_note *>(evt);
                notesOn[nevt->key] = false;
                ov->try_push(ov, evt);
            }
            break;
            }
        }
    }
};

const clap_plugin *mtsne_create_plugin(const struct clap_plugin_factory *, const clap_host *host,
                                       const char *plugin_id)
{
    _DBGCOUT << "Creating Plugin" << std::endl;
    auto *plug = new MTSNE(host);
    return plug->clapPlugin();
}

const char *features[] = {CLAP_PLUGIN_FEATURE_NOTE_EFFECT, "microtonal", "MTS-ESP", nullptr};
clap_plugin_descriptor MTSNE::desc = {CLAP_VERSION,
                                      "org.surge-synth-team.MTSToNoteExpression",
                                      "MTS To Note Expression",
                                      "Surge Synth Team",
                                      "https://surge-synth-team.org",
                                      "",
                                      "",
                                      "0.1.0",
                                      "Augment a note stream with Pitch Note Expressions to retune",
                                      features};

uint32_t mtsne_get_plugin_count(const struct clap_plugin_factory *) { return 1; }
const clap_plugin_descriptor *mtsne_get_plugin_descriptor(const struct clap_plugin_factory *,
                                                          uint32_t)
{
    return &MTSNE::desc;
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