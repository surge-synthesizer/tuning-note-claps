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
#endif // TUNING_NOTE_CLAPS_HELPERS_H
