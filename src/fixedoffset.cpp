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

#include "clap_creators.h"

#include <clap/clap.h>
#include <clap/events.h>
#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>
#include <clap/helpers/host-proxy.hh>
#include <clap/helpers/host-proxy.hxx>

#include <iostream>
#include <iomanip>
#include <array>
#include <cmath>

#include "Tunings.h"

#include "helpers.h"

struct FOFF : public clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                            clap::helpers::CheckingLevel::Minimal>
{
    FOFF(const clap_plugin_descriptor_t *desc, const clap_host *host)
        : clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                clap::helpers::CheckingLevel::Minimal>(desc, host)
    {
    }


    bool activate(double sampleRate, uint32_t minFrameCount,
                  uint32_t maxFrameCount) noexcept override
    {
        return true;
    }

    enum ParamID
    {
        offset = 0,
    };

    float neOff{24};
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
            strncpy(info->name, "FOFF Note Input", CLAP_NAME_SIZE);
        }
        else
        {
            strncpy(info->name, "FOFF Note Output", CLAP_NAME_SIZE);
        }
        return true;
    }

    static constexpr int paramIdBase = 857;
    bool implementsParams() const noexcept override { return true; }
    bool isValidParamId(clap_id paramId) const noexcept override
    {
        return paramId >= paramIdBase && paramId <= paramIdBase + paramsCount();
    }
    uint32_t paramsCount() const noexcept override { return 5; }
    bool paramsInfo(uint32_t paramIndex, clap_param_info *info) const noexcept override
    {
        info->id = paramIndex + paramIdBase;

        switch(paramIndex)
        {
        case offset:
            strncpy(info->name, "Offset in Semitones", CLAP_NAME_SIZE);
            strncpy(info->module, "", CLAP_NAME_SIZE);

            info->min_value = -48;
            info->max_value = 48;
            info->default_value = 24;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE;
            break;
        default:
            return false;
        }

        return true;
    }
    bool paramsValue(clap_id paramId, double *value) noexcept override
    {
        switch (paramId)
        {
        case paramIdBase + offset:
            *value = neOff;
            break;
        default:
            return false;
            break;
        }
        return true;
    }

    bool paramsValueToText(clap_id paramId, double value, char *display,
                           uint32_t size) noexcept override
    {
        switch (paramId)
        {
        case paramIdBase + offset:
        {
            strncpy(display, std::to_string(value).c_str(), CLAP_NAME_SIZE);
            return true;
        }
        }
        return false;
    }

    bool paramsTextToValue(clap_id paramId, const char *display, double *value) noexcept override
    {
        switch (paramId)
        {
        case paramIdBase + offset:
        {
            *value = std::atof(display);
            return true;
        }
        }
        return false;
    }

    bool implementsState() const noexcept override { return true; }
    bool stateSave(const clap_ostream *stream) noexcept override
    {
        std::map<clap_id, double> vals;
        vals[paramIdBase + offset] = neOff;
        return helpersStateSave(stream, vals);
    }
    bool stateLoad(const clap_istream *stream) noexcept override
    {
        std::map<clap_id, double> vals;
        auto res = helpersStateLoad(stream, vals);
        if (!res)
            return false;

        neOff = vals[paramIdBase + offset];

        return true;
    }

    bool tuningActive() { return true; }
    double retuningFor(int key, int channel) {
        return key + neOff;
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
                handleParamValue(pevt);
            }
            break;
            case CLAP_EVENT_MIDI:
            case CLAP_EVENT_MIDI2:
            case CLAP_EVENT_MIDI_SYSEX:
            case CLAP_EVENT_NOTE_CHOKE:
            case CLAP_EVENT_NOTE_OFF:
                ov->try_push(ov, evt);
                break;
            case CLAP_EVENT_NOTE_ON:
            {
                auto nevt = reinterpret_cast<const clap_event_note *>(evt);
                assert(nevt->channel >= 0);
                assert(nevt->channel < 16);
                assert(nevt->key >= 0);
                assert(nevt->key < 128 );

                auto q = clap_event_note_expression();
                q.header.size = sizeof(clap_event_note_expression);
                q.header.type = (uint16_t)CLAP_EVENT_NOTE_EXPRESSION;
                q.header.time = nevt->header.time;
                q.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                q.header.flags = 0;
                q.key = nevt->key;
                q.channel = nevt->channel;
                q.port_index = nevt->port_index;
                q.note_id = nevt->note_id;
                q.expression_id = CLAP_NOTE_EXPRESSION_TUNING;

                q.value = neOff;

                // If you comment this line out, bitwig won't crash
                ov->try_push(ov, evt);
                ov->try_push(ov, &(q.header));
            }
            break;

            case CLAP_EVENT_NOTE_EXPRESSION:
            {
                auto nevt = reinterpret_cast<const clap_event_note_expression *>(evt);

                auto oevt = clap_event_note_expression();
                memcpy(&oevt, evt, nevt->header.size);

                if (nevt->expression_id == CLAP_NOTE_EXPRESSION_TUNING)
                {
                    oevt.value += neOff;
                }

                ov->try_push(ov, &oevt.header);
            }
            break;
            }
        }

        return CLAP_PROCESS_CONTINUE;
    }

    void handleParamValue(const clap_event_param_value *pevt) {
        auto id = pevt->param_id;
        auto nf = pevt->value;
        switch(id)
        {
        case paramIdBase + offset:
        {
            neOff = nf;
        }
        break;
        }
    }

    void paramsFlush(const clap_input_events *in, const clap_output_events *out) noexcept override
    {
    }
};
const clap_plugin *create_foff(const clap_plugin_descriptor_t *desc, const clap_host *host)
{
    auto *plug = new FOFF(desc, host);
    return plug->clapPlugin();
}
