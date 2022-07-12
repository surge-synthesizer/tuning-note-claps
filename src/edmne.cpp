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

struct EDMNE : public clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                            clap::helpers::CheckingLevel::Minimal>
{
    EDMNE(const clap_plugin_descriptor_t *desc, const clap_host *host)
        : clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                clap::helpers::CheckingLevel::Minimal>(desc, host)
    {
        for (auto &c : noteRemaining)
            for (auto &n : c)
                n = 0.f;

        for (auto &c : sclTuning)
            for (auto &f : c)
                f = 0.f;
    }

    double secondsPerSample{0.f};
    double postNoteRelease{2.0};

    int span{2}, divisions{19}, scaleTuningCenter{69};
    double scaleTuningFrequency{440};

    int priorSpan{-1}, priorDivisions{-1}, priorCenter{-1};
    double priorFrequency{-1};

    Tunings::Tuning tuning;

    bool activate(double sampleRate, uint32_t minFrameCount,
                  uint32_t maxFrameCount) noexcept override
    {
        rebuildTuning();
        return true;
    }

    enum ParamID
    {
        octave_span = 0,
        octave_divisions,
        center,
        frequency,
        release
    };

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
            strncpy(info->name, "EDMN Note Input", CLAP_NAME_SIZE);
        }
        else
        {
            strncpy(info->name, "EDMN Note Output", CLAP_NAME_SIZE);
        }
        return true;
    }

    static constexpr int paramIdBase = 187632;
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
        case octave_span:
            strncpy(info->name, "Even Division Of", CLAP_NAME_SIZE);
            strncpy(info->module, "", CLAP_NAME_SIZE);

            info->min_value = 2;
            info->max_value = 6;
            info->default_value = 2;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED;
            break;
        case octave_divisions:
            strncpy(info->name, "Into Steps", CLAP_NAME_SIZE);
            strncpy(info->module, "", CLAP_NAME_SIZE);

            info->min_value = 3;
            info->max_value = 72;
            info->default_value = 19;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED;
            break;
        case center:
            strncpy(info->name, "Tuning Center Key", CLAP_NAME_SIZE);
            strncpy(info->module, "", CLAP_NAME_SIZE);

            info->min_value = 0;
            info->max_value = 127;
            info->default_value = 69;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED;
            break;
        case frequency:
            strncpy(info->name, "Tuning Center Frequency", CLAP_NAME_SIZE);
            strncpy(info->module, "", CLAP_NAME_SIZE);

            info->min_value = 220;
            info->max_value = 880;
            info->default_value = 440;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE;
            break;
        case release:
            strncpy(info->name, "Post Note Release (s)", CLAP_NAME_SIZE);
            strncpy(info->module, "", CLAP_NAME_SIZE);

            info->min_value = 0;
            info->max_value = 16;
            info->default_value = 2;
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
        case paramIdBase + octave_span:
            *value = span;
            break;
        case paramIdBase + octave_divisions:
            *value = divisions;
            break;
        case paramIdBase + center:
            *value = scaleTuningCenter;
            break;
        case paramIdBase + frequency:
            *value = scaleTuningFrequency;
            break;
        case paramIdBase + release:
            *value = postNoteRelease;
            break;
        }
        return true;
    }

    bool paramsValueToText(clap_id paramId, double value, char *display,
                           uint32_t size) noexcept override
    {
        switch (paramId)
        {
        case paramIdBase + octave_divisions:
        case paramIdBase + octave_span:
        case paramIdBase + center:
        {
            strncpy(display, std::to_string((int)value).c_str(), CLAP_NAME_SIZE);
            return true;
        }
        case paramIdBase + frequency:
        {
            std::ostringstream oss;
            oss << std::setprecision(8) << value << " Hz";
            strncpy(display, oss.str().c_str(), CLAP_NAME_SIZE);
            return true;
        }
        case paramIdBase + release:
        {
            std::ostringstream oss;
            oss << std::setprecision(4) << value << " s";
            strncpy(display, oss.str().c_str(), CLAP_NAME_SIZE);
            return true;
        }
        }
        return false;
    }

    bool paramsTextToValue(clap_id paramId, const char *display, double *value) noexcept override
    {
        switch (paramId)
        {
        case paramIdBase + octave_divisions:
        case paramIdBase + octave_span:
        case paramIdBase + center:
        {
            *value = std::atoi(display);
            return true;
        }
        case paramIdBase + frequency:
        case paramIdBase + release:
        {
            *value = std::atof(display);
            return true;
        }
        }
        return false;
    }

    char priorScaleName[CLAP_NAME_SIZE];
    std::array<std::array<float, 128>, 16> noteRemaining; // -1 means still held, otherwise its the time
    std::array<std::array<double, 128>, 16> sclTuning;

    bool implementsState() const noexcept override { return true; }
    bool stateSave(const clap_ostream *stream) noexcept override
    {
        std::map<clap_id, double> vals;
        vals[paramIdBase + octave_span] = span;
        vals[paramIdBase + octave_divisions] = divisions;
        vals[paramIdBase + center] = scaleTuningCenter;
        vals[paramIdBase + frequency] = scaleTuningFrequency;
        vals[paramIdBase + release] = postNoteRelease;
        return helpersStateSave(stream, vals);
    }
    bool stateLoad(const clap_istream *stream) noexcept override
    {
        std::map<clap_id, double> vals;
        auto res = helpersStateLoad(stream, vals);
        if (!res)
            return false;

        span = vals[paramIdBase + octave_span];
        divisions = vals[paramIdBase + octave_divisions];
        scaleTuningCenter = vals[paramIdBase + center];
        scaleTuningFrequency = vals[paramIdBase + frequency];
        postNoteRelease = vals[paramIdBase + release];

        return true;
    }

    bool tuningActive() { return true; }
    double retuningFor(int key, int channel) {
        return sclTuning[0][key];
    }

    clap_process_status process(const clap_process *process) noexcept override
    {
        processTuningCore(this, process);
        return CLAP_PROCESS_CONTINUE;
    }

    void rebuildTuning()
    {
        if (frequency == priorFrequency && span == priorSpan &&
            divisions == priorDivisions && center == priorCenter)
            return;

        priorFrequency = scaleTuningFrequency;
        priorSpan = span;
        priorDivisions = divisions;
        priorCenter = scaleTuningCenter;
        auto sc = Tunings::evenDivisionOfSpanByM(span, divisions);
        auto km = Tunings::tuneNoteTo(scaleTuningCenter, scaleTuningFrequency);
        tuning = Tunings::Tuning(sc, km);
        auto ed212 = Tunings::Tuning();

        for (int k=0; k<127; ++k)
        {
            auto mt = tuning.logScaledFrequencyForMidiNote(k);
            auto et = ed212.logScaledFrequencyForMidiNote(k);
            auto diff = mt - et;
            sclTuning[0][k] = diff * 12.0;
        }
        for (int c=1;c<16;++c)
        {
            for (int k=0; k<127; ++k)
            {
                sclTuning[c][k] = sclTuning[0][k];
            }
        }

    }
    void handleParamValue(const clap_event_param_value *pevt) {
        auto id = pevt->param_id;
        auto nf = pevt->value;
        switch(id)
        {
        case paramIdBase + octave_span:
        {
            span = std::clamp(static_cast<int>(std::round(nf)), 2, 6);
            rebuildTuning();
        }
        break;
        case paramIdBase + octave_divisions:
        {
            divisions = std::clamp(static_cast<int>(std::round(nf)), 3, 72);
            rebuildTuning();
        }
        break;
        case paramIdBase + center:
        {
            scaleTuningCenter = std::clamp(static_cast<int>(std::round(nf)), 0, 127);
            rebuildTuning();
        }
        break;
        case paramIdBase + frequency:
        {
            scaleTuningFrequency = std::clamp(nf, 220.0, 880.0);
            rebuildTuning();
        }
        break;
        case paramIdBase + release:
        {
            postNoteRelease = std::clamp(nf, 0., 100.);
            rebuildTuning();
        }
        break;
        }
    }

    void paramsFlush(const clap_input_events *in, const clap_output_events *out) noexcept override
    {
        paramsFlushTuningCore(this, in, out);
    }
};
const clap_plugin *create_ednmne(const clap_plugin_descriptor_t *desc, const clap_host *host)
{
    auto *plug = new EDMNE(desc, host);
    return plug->clapPlugin();
}
