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

#include "helpers.h"

#include "libMTSClient.h"

#define _DBGCOUT std::cout << __FILE__ << ":" << __LINE__ << " | "

struct MTSNE : public clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                            clap::helpers::CheckingLevel::Minimal>
{
    MTSNE(const clap_plugin_descriptor_t *desc, const clap_host *host)
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

    MTSClient *mtsClient{nullptr};
    double secondsPerSample{0.f};

    double postNoteRelease{2.0};
    int dummyMtsValue{0};

    bool activate(double sampleRate, uint32_t minFrameCount,
                  uint32_t maxFrameCount) noexcept override
    {
        mtsClient = MTS_RegisterClient();
        priorScaleName[0] = 0;
        if (MTS_HasMaster(mtsClient))
            strncpy(priorScaleName, MTS_GetScaleName(mtsClient), CLAP_NAME_SIZE);
        secondsPerSample = 1.0 / sampleRate;
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

    static constexpr int paramIdBase = 54082;
    bool implementsParams() const noexcept override { return true; }
    bool isValidParamId(clap_id paramId) const noexcept override
    {
        return paramId >= paramIdBase && paramId <= paramIdBase + paramsCount();
    }
    uint32_t paramsCount() const noexcept override { return 2; }
    bool paramsInfo(uint32_t paramIndex, clap_param_info *info) const noexcept override
    {
        info->id = paramIndex + paramIdBase;

        switch(paramIndex)
        {
        case 0:
            strncpy(info->name, "MTS Connection Status", CLAP_NAME_SIZE);
            strncpy(info->module, "", CLAP_NAME_SIZE);

            info->min_value = 0;
            info->max_value = 0;
            info->default_value = 0;
            info->flags = /* CLAP_PARAM_IS_READONLY | */ CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED;
            break;
        case 1:
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
        case paramIdBase + 0:
            *value = dummyMtsValue;
            break;
        case paramIdBase + 1:
            *value = postNoteRelease;
            break;
        }
        return true;
    }

    static constexpr const char* disconLabel = "No MTS Connection";
    bool paramsValueToText(clap_id paramId, double value, char *display,
                           uint32_t size) noexcept override
    {
        switch (paramId)
        {
        case paramIdBase + 0:
        {
            if (mtsClient && MTS_HasMaster(mtsClient))
            {
                std::ostringstream oss;
                oss << "MTS: " << MTS_GetScaleName(mtsClient);
                strncpy(display, oss.str().c_str(), CLAP_NAME_SIZE);
            }
            else
            {
                strncpy(display, disconLabel, CLAP_NAME_SIZE);
            }
            return true;
        }
        case paramIdBase + 1:
        {
            std::ostringstream oss;
            oss << std::setprecision(2) << value << " s";
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
        case paramIdBase + 0:
        {
            *value = 0;
            return true;
        }
        case paramIdBase + 1:
        {
            *value = std::atof(display);
            return true;
        }
        }
        return false;
    }

    char priorScaleName[CLAP_NAME_SIZE];
    std::array<std::array<float, 127>, 16> noteRemaining; // -1 means still held, otherwise its the time
    std::array<std::array<double, 127>, 16> sclTuning;

    void onMainThread() noexcept override {
        // Scale name has changed. We need to send events
        if (_host.canUseParams())
            _host.paramsRescan(CLAP_PARAM_RESCAN_TEXT);
        if (mtsClient && MTS_HasMaster(mtsClient))
            strncpy(priorScaleName, MTS_GetScaleName(mtsClient), CLAP_NAME_SIZE);
        else
            strncpy(priorScaleName, disconLabel, CLAP_NAME_SIZE);
    }

    bool implementsState() const noexcept override { return true; }
    bool stateSave(const clap_ostream *stream) noexcept override
    {
        std::map<clap_id, double> vals;
        vals[paramIdBase + 1] = postNoteRelease;
        return helpersStateSave(stream, vals);
    }
    bool stateLoad(const clap_istream *stream) noexcept override
    {
        std::map<clap_id, double> vals;
        auto res = helpersStateLoad(stream, vals);
        if (!res)
            return false;

        postNoteRelease = vals[paramIdBase + 1];

        return true;
    }

    clap_process_status process(const clap_process *process) noexcept override
    {
        if (mtsClient && MTS_HasMaster(mtsClient) && strncmp(priorScaleName, MTS_GetScaleName(mtsClient), CLAP_NAME_SIZE) != 0)
        {
            _host.requestCallback();
        }

        if (mtsClient && !MTS_HasMaster(mtsClient) && strncmp(priorScaleName, disconLabel, CLAP_NAME_SIZE) != 0)
        {
            _host.requestCallback();
        }

        processTuningCore(this, process);

        return CLAP_PROCESS_CONTINUE;
    }

    void handleParamValue(const clap_event_param_value *pevt)
    {
        auto id = pevt->param_id;
        auto nf = pevt->value;
        if (id == paramIdBase + 0)
        {
            postNoteRelease = nf;
        }
    }
    bool tuningActive() const {
        return mtsClient && MTS_HasMaster(mtsClient);
    }

    float retuningFor(int key, int channel) const
    {
        return MTS_RetuningInSemitones(mtsClient, key, channel);
    }

    void paramsFlush(const clap_input_events *in, const clap_output_events *out) noexcept override
    {
        paramsFlushTuningCore(this, in, out);
    }
};

const clap_plugin *create_mtsne(const clap_plugin_descriptor_t *desc, const clap_host *host)
{
    auto *plug = new MTSNE(desc, host);
    return plug->clapPlugin();
}
