#include <doctest.h>
#include "../../Source/MIDI/MidiController.h"

using namespace af;

TEST_CASE ("MIDI CC mapping drives the intended parameter")
{
    ParameterStore p;
    MidiController m (p);
    m.clearAll();
    m.setMapping ({ 20, pAmpGain, 0, 1, false });
    const uint8_t cc[] = { 0xB0, 20, 127 };
    m.handleMessage (cc, 3);
    CHECK (p.get (pAmpGain) == doctest::Approx (1.0f));
    const uint8_t cc2[] = { 0xB3, 20, 0 }; // other channel, omni
    m.handleMessage (cc2, 3);
    CHECK (p.get (pAmpGain) == doctest::Approx (0.0f));

    m.setChannel (2);
    const uint8_t wrongCh[] = { 0xB0, 20, 127 };
    m.handleMessage (wrongCh, 3);
    CHECK (p.get (pAmpGain) == doctest::Approx (0.0f));
    const uint8_t rightCh[] = { 0xB1, 20, 64 };
    m.handleMessage (rightCh, 3);
    CHECK (p.get (pAmpGain) == doctest::Approx (64.0f / 127.0f));
}

TEST_CASE ("Toggle parameters: latching and toggle footswitch modes")
{
    ParameterStore p;
    MidiController m (p);
    m.clearAll();
    m.setMapping ({ 80, pDelayOn, 0, 1, false });
    const uint8_t on[] = { 0xB0, 80, 127 }, off[] = { 0xB0, 80, 0 };
    m.handleMessage (on, 3);
    CHECK (p.getBool (pDelayOn));
    m.handleMessage (off, 3);
    CHECK_FALSE (p.getBool (pDelayOn));

    m.setMapping ({ 81, pReverbOn, 0, 1, true });
    const uint8_t press[] = { 0xB0, 81, 127 }, release[] = { 0xB0, 81, 0 };
    m.handleMessage (press, 3);
    m.handleMessage (release, 3);
    CHECK (p.getBool (pReverbOn));
    m.handleMessage (press, 3);
    CHECK_FALSE (p.getBool (pReverbOn));
}

TEST_CASE ("Range-limited output level mapping")
{
    ParameterStore p;
    MidiController m (p); // defaults include CC7 -> out.level limited to -40..0 dB
    const uint8_t full[] = { 0xB0, 7, 127 }, zero[] = { 0xB0, 7, 0 };
    m.handleMessage (full, 3);
    CHECK (p.get (pOutLevel) == doctest::Approx (0.0f).epsilon (0.01));
    m.handleMessage (zero, 3);
    CHECK (p.get (pOutLevel) == doctest::Approx (-40.0f).epsilon (0.01));
}

TEST_CASE ("Learn mode, program change and malformed messages")
{
    ParameterStore p;
    MidiController m (p);
    m.startLearn (pEq750);
    const uint8_t cc[] = { 0xB0, 33, 100 };
    m.handleMessage (cc, 3);
    CHECK (m.getLearnParam() == -1);
    CHECK (m.findCCForParam (pEq750) == 33);
    m.handleMessage (cc, 3);
    CHECK (p.get (pEq750) == doctest::Approx (normalisedToPlain (pEq750, 100.0f / 127.0f)));

    const uint8_t pc[] = { 0xC0, 5 };
    m.handleMessage (pc, 2);
    CHECK (m.takePendingProgram() == 5);
    CHECK (m.takePendingProgram() == -1);
    m.setProgramChangeEnabled (false);
    m.handleMessage (pc, 2);
    CHECK (m.takePendingProgram() == -1);

    const auto before = p.snapshot();
    const uint8_t bad1[] = { 0xB0, 20 }, bad2[] = { 0x20, 1, 2 }, bad3[] = { 0xB0, 200, 1 }, sysex[] = { 0xF0, 1, 2, 0xF7 };
    m.handleMessage (bad1, 2);
    m.handleMessage (bad2, 3);
    m.handleMessage (bad3, 3);
    m.handleMessage (sysex, 4);
    m.handleMessage (nullptr, 0);
    CHECK (p.snapshot() == before);
}

TEST_CASE ("Mapping serialisation round trip ignores junk")
{
    ParameterStore p;
    MidiController m (p);
    m.setMapping ({ 99, pReverbMix, 0.25f, 0.75f, false });
    const auto text = m.serialise();
    MidiController m2 (p);
    m2.deserialise (text + "garbage;1,unknown.param,0,1,0;x,y;");
    CHECK (m2.serialise() == text);
}
