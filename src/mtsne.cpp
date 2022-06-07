#include <clap/clap.h>
#include <clap/events.h>
#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>
#include <clap/helpers/host-proxy.hh>
#include <clap/helpers/host-proxy.hxx>

#include <iostream>
#include <array>
#include <cmath>

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
        for (auto &f : sclTuning)
            f = 0.f;
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
        return true;
    }

    bool implementsParams() const noexcept override { return true; }
    bool isValidParamId(clap_id paramId) const noexcept override
    {
        return paramId >= 1 && paramId <= 1;
    }
    uint32_t paramsCount() const noexcept override { return 1; }
    bool paramsInfo(uint32_t paramIndex, clap_param_info *info) const noexcept override
    {
        // Fixme - using parameter groups here would be lovely but until then
        info->id = paramIndex + 1;
        strncpy(info->name, "ED2 Into N", CLAP_NAME_SIZE);
        strncpy(info->module, "Scale", CLAP_NAME_SIZE);

        info->min_value = 4;
        info->max_value = 59;
        info->default_value = 19;
        info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED;

        return true;
    }
    bool paramsValue(clap_id paramId, double *value) noexcept override
    {
        switch (paramId)
        {
        case 1:
            *value = edN;
            break;
        }
        return true;
    }

    bool paramsValueToText(clap_id paramId, double value, char *display,
                           uint32_t size) noexcept override
    {
        switch (paramId)
        {
        case 1:
        {
            std::string s = "ED2/" + std::to_string((int)value);
            strncpy(display, s.c_str(), CLAP_NAME_SIZE);
            break;
        }
        }
        return true;
    }

    int edN{19};
    std::array<bool, 127> notesOn;
    std::array<float, 127> sclTuning;

    template <typename T> void dup(const clap_event_header_t *evt, const clap_output_events *ov)
    {
        /*
        auto oevt = T();
        memcpy(&oevt, evt, evt->size);
        ov->try_push(ov, reinterpret_cast<clap_event_header_t *>(&oevt));
         */
        ov->try_push(ov, evt);
    }

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
            case CLAP_EVENT_PARAM_VALUE:
            {
                auto pevt = reinterpret_cast<const clap_event_param_value *>(evt);

                auto id = pevt->param_id;
                auto nf = pevt->value;
                if (id == 1)
                {
                    edN = (int)(std::round(nf));
                }
            }
            break;
            case CLAP_EVENT_MIDI:
                dup<clap_event_midi>(evt, ov);
                break;
            case CLAP_EVENT_MIDI2:
                dup<clap_event_midi2>(evt, ov);
                break;
            case CLAP_EVENT_NOTE_CHOKE:
                dup<clap_event_note>(evt, ov);
                break;
            case CLAP_EVENT_NOTE_ON:
            {
                auto nevt = reinterpret_cast<const clap_event_note *>(evt);
                notesOn[nevt->key] = true;
                sclTuning[nevt->key] = 0.f;

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
                auto retune = dFrom60 * 12.0 / edN - dFrom60;

                sclTuning[nevt->key] = retune;

                q.value = retune;

                dup<clap_event_note>(evt, ov);
                ov->try_push(ov, reinterpret_cast<const clap_event_header *>(&q));
            }
            break;
            case CLAP_EVENT_NOTE_OFF:
            {
                auto nevt = reinterpret_cast<const clap_event_note *>(evt);
                notesOn[nevt->key] = false;
                dup<clap_event_note>(evt, ov);
            }
            break;
            case CLAP_EVENT_NOTE_EXPRESSION:
            {
                auto nevt = reinterpret_cast<const clap_event_note_expression *>(evt);

                auto oevt = clap_event_note_expression();
                memcpy(&oevt, evt, nevt->header.size);

                if (nevt->expression_id == CLAP_NOTE_EXPRESSION_TUNING)
                {
                    oevt.value += sclTuning[nevt->key];
                }

                ov->try_push(ov, reinterpret_cast<const clap_event_header *>(&oevt));
            }
            break;
            }
        }

        return CLAP_PROCESS_CONTINUE;
    }
};

const clap_plugin *mtsne_create_plugin(const struct clap_plugin_factory *, const clap_host *host,
                                       const char *plugin_id)
{
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