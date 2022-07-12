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

#ifndef TUNING_NOTE_CLAPS_HELPERS_H
#define TUNING_NOTE_CLAPS_HELPERS_H

#include <clap/plugin.h>
#include <clap/ext/state.h>
#include <string>
#include <sstream>
#include <iomanip>
#include <clocale>

#include <map>
#include <string>
#include <vector>

inline bool helpersStateSave(const clap_ostream *stream, const std::map<clap_id, double> &paramToValue) noexcept
{
    std::ostringstream oss;
    auto cloc = std::locale("C");
    oss.imbue(cloc);
    oss << "STREAM-VERSION-1;";
    for (const auto &[id, val] : paramToValue)
    {
        oss << id << "=" << std::setw(30) << std::setprecision(20) << val << ";";
    }

    auto st = oss.str();
    auto c = st.c_str();
    auto s = st.length() + 1; // write the null terminator
    while (s > 0)
    {
        auto r = stream->write(stream, c, s);
        if (r < 0)
            return false;
        s -= r;
        c += r;
    }
    return true;
}

inline bool helpersStateLoad(const clap_istream *stream, std::map<clap_id, double> &paramToValue) noexcept
{
    static constexpr uint32_t maxSize = 4096 * 8, chunkSize = 256;
    char buffer[maxSize];
    char *bp = &(buffer[0]);
    int64_t rd{0};
    int64_t totalRd{0};

    buffer[0] = 0;
    while ((rd = stream->read(stream, bp, chunkSize)) > 0)
    {
        bp += rd;
        totalRd += rd;
        if (totalRd >= maxSize - chunkSize - 1)
        {
            // What the heck? You sdent me more than 32kb of data for a 700 byte string?
            // That means my next chunk read will blow out memory so....
            return false;
        }
    }

    // Make sure I'm null terminated in case you hand me total garbage
    if (totalRd < maxSize)
        buffer[totalRd] = 0;

    auto dat = std::string(buffer);

    std::vector<std::string> items;
    size_t spos{0};
    while ((spos = dat.find(';')) != std::string::npos)
    {
        auto l = dat.substr(0, spos);
        dat = dat.substr(spos + 1);
        items.push_back(l);
    }

    if (items[0] != "STREAM-VERSION-1")
    {
        return false;
    }
    for (auto i : items)
    {
        auto epos = i.find('=');
        if (epos == std::string::npos)
            continue; // oh well
        auto id = std::atoi(i.substr(0, epos).c_str());
        double val = 0.0;
        std::istringstream istr(i.substr(epos + 1));
        istr.imbue(std::locale("C"));
        istr >> val;

        paramToValue[(clap_id)id]= val;
    }

    return true;
}

template<typename T>
inline void processTuningCore(T *that, const clap_process *process)
{
    auto ev = process->in_events;
    auto ov = process->out_events;
    auto sz = ev->size(ev);


    auto &sclTuning = that->sclTuning;
    // Generate top-of-block tuning messages for all our notes that are on
    for (int c = 0; c < 16; ++c)
    {
        for (int i = 0; i < 128; ++i)
        {
            if (that->tuningActive() && that->noteRemaining[c][i] != 0.f)
            {
                auto prior = sclTuning[c][i];
                sclTuning[c][i] = that->retuningFor(i, c);
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

            that->handleParamValue(pevt);
        }
        break;
        case CLAP_EVENT_MIDI:
        case CLAP_EVENT_MIDI2:
        case CLAP_EVENT_MIDI_SYSEX:
        case CLAP_EVENT_NOTE_CHOKE:
            ov->try_push(ov, evt);
            break;
        case CLAP_EVENT_NOTE_ON:
        {
            auto nevt = reinterpret_cast<const clap_event_note *>(evt);
            assert(nevt->channel >= 0);
            assert(nevt->channel < 16);
            assert(nevt->key >= 0);
            assert(nevt->key < 128 );
            that->noteRemaining[nevt->channel][nevt->key] = -1;

            ov->try_push(ov, evt);

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

            if (that->tuningActive())
            {
                sclTuning[nevt->channel][nevt->key] = that->retuningFor(nevt->key, nevt->channel);
            }
            q.value = sclTuning[nevt->channel][nevt->key];

            // If you comment this line out, bitwig won't crash
            ov->try_push(ov, &(q.header));
        }
        break;
        case CLAP_EVENT_NOTE_OFF:
        {
            auto nevt = reinterpret_cast<const clap_event_note *>(evt);
            assert(nevt->channel >= 0);
            assert(nevt->channel < 16);
            assert(nevt->key >= 0);
            assert(nevt->key < 128 );
            that->noteRemaining[nevt->channel][nevt->key] = that->postNoteRelease;
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
                if (that->tuningActive())
                {
                    sclTuning[nevt->channel][nevt->key] = that->retuningFor(nevt->key, nevt->channel);
                }
                oevt.value += sclTuning[nevt->channel][nevt->key];
            }

            ov->try_push(ov, &oevt.header);
         }
        break;
        }
    }

    // subtract block size seconds from everyone with remaining time and zero out some
    for (auto &c : that->noteRemaining)
        for (auto &n : c)
            if (n > 0.f)
            {
                n -= that->secondsPerSample * process->frames_count;
                if (n < 0)
                    n = 0.f;
            }
}

template<typename T>
void paramsFlushTuningCore(T *that, const clap_input_events *in, const clap_output_events *out) noexcept
{
    auto ev = in;
    auto sz = in->size(in);
    for (uint32_t i = 0; i < sz; ++i)
    {
        auto evt = ev->get(ev, i);
        switch (evt->type)
        {
        case CLAP_EVENT_PARAM_VALUE:
        {
            auto pevt = reinterpret_cast<const clap_event_param_value *>(evt);

            that->handleParamValue(pevt);
        }
        }
    }
}
#endif // TUNING_NOTE_CLAPS_HELPERS_H
