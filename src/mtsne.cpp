#include <clap/clap.h>
#include <clap/events.h>
#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>
#include <clap/helpers/host-proxy.hh>
#include <clap/helpers/host-proxy.hxx>

#include <iostream>
#include <array>
#include <cmath>

#include "libMTSClient.h"

#define _DBGCOUT std::cout << __FILE__ << ":" << __LINE__ << " | "

struct MTSNE : public clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                            clap::helpers::CheckingLevel::Minimal>
{
    MTSNE(const clap_plugin_descriptor_t *desc, const clap_host *host)
        : clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                clap::helpers::CheckingLevel::Minimal>(desc, host)
    {
        for (auto &c : notesOn)
            for (auto &n : c)
                n = false;

        for (auto &c : sclTuning)
            for (auto &f : c)
                f = 0.f;
    }

  protected:
    MTSClient *mtsClient{nullptr};
    bool activate(double sampleRate, uint32_t minFrameCount,
                  uint32_t maxFrameCount) noexcept override
    {
        mtsClient = MTS_RegisterClient();
        _DBGCOUT << "MTS Client is " << mtsClient << std::endl;
        return true;
    }

    void deactivate() noexcept override
    {
        if (mtsClient)
            MTS_DeregisterClient(mtsClient);
    }

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
    std::array<std::array<bool, 127>, 16> notesOn;
    std::array<std::array<double, 127>, 16> sclTuning;

    template <typename T> void dup(const clap_event_header_t *evt, const clap_output_events *ov)
    {
        ov->try_push(ov, evt);
    }

    clap_process_status process(const clap_process *process) noexcept override
    {
        auto ev = process->in_events;
        auto ov = process->out_events;
        auto sz = ev->size(ev);

        // Generate top-of-block tuning messages for all our notes that are on
        for (int c = 0; c < 16; ++c)
        {
            for (int i = 0; i < 127; ++i)
            {
                if (mtsClient && notesOn[c][i])
                {
                    auto prior = sclTuning[c][i];
                    sclTuning[c][i] = MTS_RetuningInSemitones(mtsClient, i, c);
                    if (sclTuning[c][i] != prior)
                    {
                        auto q = clap_event_note_expression();
                        q.header.size = sizeof(clap_event_note_expression);
                        q.header.type = (uint16_t)CLAP_EVENT_NOTE_EXPRESSION;
                        q.header.time = 0;
                        q.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                        q.header.flags = 0;
                        q.key = i;
                        q.channel = c;
                        q.port_index = 0;
                        q.expression_id = CLAP_NOTE_EXPRESSION_TUNING;

                        q.value = sclTuning[c][i];

                        ov->try_push(ov, reinterpret_cast<const clap_event_header *>(&q));
                    }
                }
            }
        }

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
            case CLAP_EVENT_MIDI2:
            case CLAP_EVENT_NOTE_CHOKE:
                ov->try_push(ov, evt);
                break;
            case CLAP_EVENT_NOTE_ON:
            {
                auto nevt = reinterpret_cast<const clap_event_note *>(evt);
                notesOn[nevt->channel][nevt->key] = true;

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

                if (mtsClient)
                {
                    sclTuning[nevt->channel][nevt->key] =
                        MTS_RetuningInSemitones(mtsClient, nevt->key, nevt->channel);
                }
                q.value = sclTuning[nevt->channel][nevt->key];

                ov->try_push(ov, evt);
                ov->try_push(ov, reinterpret_cast<const clap_event_header *>(&q));
            }
            break;
            case CLAP_EVENT_NOTE_OFF:
            {
                auto nevt = reinterpret_cast<const clap_event_note *>(evt);
                notesOn[nevt->channel][nevt->key] = false;
                ov->try_push(ov, evt);
            }
            break;
            case CLAP_EVENT_NOTE_EXPRESSION:
            {
                auto nevt = reinterpret_cast<const clap_event_note_expression *>(evt);

                auto oevt = clap_event_note_expression();
                memcpy(&oevt, evt, nevt->header.size);

                if (nevt->expression_id == CLAP_NOTE_EXPRESSION_TUNING)
                {
                    oevt.value += sclTuning[nevt->channel][nevt->key];
                }

                ov->try_push(ov, reinterpret_cast<const clap_event_header *>(&oevt));
            }
            break;
            }
        }

        return CLAP_PROCESS_CONTINUE;
    }
};

const clap_plugin *create_mtsne(const clap_plugin_descriptor_t *desc, const clap_host *host)
{
    auto *plug = new MTSNE(desc, host);
    return plug->clapPlugin();
}
