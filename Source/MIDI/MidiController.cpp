#include "MidiController.h"

#include <algorithm>
#include <sstream>

namespace af
{
MidiController::MidiController (ParameterStore& store) : params (store) { loadDefaultMappings(); }

void MidiController::handleMessage (const uint8_t* data, int size) noexcept
{
    if (data == nullptr || size < 1)
        return;
    const uint8_t status = data[0];
    if (status < 0x80 || status >= 0xF0) // running status / system messages are not used
        return;
    const int type = status & 0xF0;
    const int ch = (status & 0x0F) + 1;
    const int filter = channel.load (std::memory_order_relaxed);
    if (filter != 0 && filter != ch)
        return;

    if (type == 0xB0)
    {
        if (size < 3 || data[1] > 127 || data[2] > 127)
            return;
        const int cc = data[1];
        const int value = data[2];
        activity.fetch_add (1, std::memory_order_relaxed);
        lastCC.store (cc, std::memory_order_relaxed);

        const int learn = learnParam.exchange (-1);
        if (learn >= 0 && learn < kNumParams)
        {
            // Unmap the parameter from any previous CC so one control drives it.
            for (auto& s : slots)
                if (s.param.load() == learn)
                    s.param.store (-1);
            const bool isToggle = paramDef (learn).scale == ParamScale::toggle;
            slots[(size_t) cc].minNorm.store (0.0f);
            slots[(size_t) cc].maxNorm.store (1.0f);
            slots[(size_t) cc].toggle.store (isToggle);
            slots[(size_t) cc].param.store (learn);
            return; // the learning message itself does not move the control
        }

        auto& s = slots[(size_t) cc];
        const int p = s.param.load (std::memory_order_relaxed);
        if (p < 0 || p >= kNumParams)
            return;

        const auto& def = paramDef (p);
        if (def.scale == ParamScale::toggle)
        {
            if (s.toggle.load (std::memory_order_relaxed))
            {
                if (value >= 64)
                    params.set (p, params.getBool (p) ? 0.0f : 1.0f);
            }
            else
                params.set (p, value >= 64 ? 1.0f : 0.0f);
            return;
        }
        const float lo = s.minNorm.load (std::memory_order_relaxed);
        const float hi = s.maxNorm.load (std::memory_order_relaxed);
        params.setNormalised (p, lo + (hi - lo) * (float) value / 127.0f);
    }
    else if (type == 0xC0)
    {
        if (size < 2 || data[1] > 127)
            return;
        activity.fetch_add (1, std::memory_order_relaxed);
        if (pcEnabled.load (std::memory_order_relaxed))
            pendingProgram.store (data[1]);
    }
}

void MidiController::setMapping (const CCMapping& m) noexcept
{
    if (m.cc < 0 || m.cc > 127)
        return;
    auto& s = slots[(size_t) m.cc];
    s.minNorm.store (std::clamp (m.minNorm, 0.0f, 1.0f));
    s.maxNorm.store (std::clamp (m.maxNorm, 0.0f, 1.0f));
    s.toggle.store (m.toggle);
    s.param.store (m.paramIndex >= 0 && m.paramIndex < kNumParams ? m.paramIndex : -1);
}

void MidiController::clearMapping (int cc) noexcept
{
    if (cc >= 0 && cc < 128)
        slots[(size_t) cc].param.store (-1);
}

void MidiController::clearAll() noexcept
{
    for (auto& s : slots)
        s.param.store (-1);
}

std::vector<CCMapping> MidiController::getMappings() const
{
    std::vector<CCMapping> out;
    for (int cc = 0; cc < 128; ++cc)
    {
        const auto& s = slots[(size_t) cc];
        const int p = s.param.load();
        if (p >= 0)
            out.push_back ({ cc, p, s.minNorm.load(), s.maxNorm.load(), s.toggle.load() });
    }
    return out;
}

int MidiController::findCCForParam (int paramIndex) const noexcept
{
    for (int cc = 0; cc < 128; ++cc)
        if (slots[(size_t) cc].param.load() == paramIndex)
            return cc;
    return -1;
}

void MidiController::loadDefaultMappings() noexcept
{
    clearAll();
    // Common footswitch CCs (latching: 127 = on, 0 = off).
    setMapping ({ 80, pDriveOn, 0, 1, false });
    setMapping ({ 81, pEqOn, 0, 1, false });
    setMapping ({ 82, pDelayOn, 0, 1, false });
    setMapping ({ 83, pReverbOn, 0, 1, false });
    setMapping ({ 84, pChorusOn, 0, 1, false });
    setMapping ({ 85, pGateOn, 0, 1, false });
    // Expression / continuous.
    setMapping ({ 11, pAmpGain, 0, 1, false });
    setMapping ({ 12, pDelayMix, 0, 1, false });
    setMapping ({ 13, pReverbMix, 0, 1, false });
    // Output level limited to -40..0 dB so a pedal cannot exceed unity.
    setMapping ({ 7, pOutLevel, plainToNormalised (pOutLevel, -40.0f), plainToNormalised (pOutLevel, 0.0f), false });
}

std::string MidiController::serialise() const
{
    std::ostringstream os;
    for (const auto& m : getMappings())
        os << m.cc << ',' << paramDef (m.paramIndex).id << ',' << m.minNorm << ',' << m.maxNorm << ',' << (m.toggle ? 1 : 0) << ';';
    return os.str();
}

void MidiController::deserialise (const std::string& s)
{
    clearAll();
    std::istringstream is (s);
    std::string entry;
    while (std::getline (is, entry, ';'))
    {
        std::istringstream es (entry);
        std::string ccS, id, minS, maxS, togS;
        if (! std::getline (es, ccS, ',') || ! std::getline (es, id, ',') || ! std::getline (es, minS, ',')
            || ! std::getline (es, maxS, ',') || ! std::getline (es, togS, ','))
            continue;
        try
        {
            const int idx = findParamIndex (id);
            if (idx < 0)
                continue;
            setMapping ({ std::stoi (ccS), idx, std::stof (minS), std::stof (maxS), togS == "1" });
        }
        catch (...)
        {
            // malformed entry: skip it
        }
    }
}

} // namespace af
