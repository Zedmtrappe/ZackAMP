// AMP FORGE — amplifier models.
//
// Each model ("voicing") is a short chain of circuit-inspired blocks — triode stages,
// gain pots, cathode followers, the passive tone stack at its model-specific position —
// followed by a power amp. The families differ in topology, not just EQ:
//
//   American  : V1 -> tone stack -> volume (bright cap) -> V2 -> power   (clean headroom)
//               V1 -> volume -> V2 -> cathode follower -> tone stack -> power (tweed-style)
//   British   : V1 -> volume (bright) -> V2 -> cathode follower -> tone stack -> power
//               V1 -> gain -> cold V2 -> V3 -> cathode follower -> tone stack -> power
//   Modern    : V1 -> tone stack (before the gain!) -> gain -> V2..V4 -> [graphic EQ] -> power
//               V1 -> gain -> V2..V4 -> cathode follower -> tone stack -> [graphic EQ] -> power
//
// None of these reproduces a specific commercial circuit; they borrow the well-known
// design principles of the classic amplifier categories, with original values.
#pragma once

#include "TubeStage.h"
#include "../PowerAmp/PowerAmp.h"
#include "../ToneStacks/ToneStack.h"

#include <array>
#include <cstdint>

namespace af::dsp
{
enum class AmpFamily : uint8_t { american, british, modern };

enum AmpControlBits : uint32_t
{
    ctlGain = 1u << 0,
    ctlBass = 1u << 1,
    ctlMid = 1u << 2,
    ctlTreble = 1u << 3,
    ctlPresence = 1u << 4,
    ctlDepth = 1u << 5,
    ctlMaster = 1u << 6,
    ctlBright = 1u << 7,
};

enum class BlockKind : uint8_t { none, triode, gainPot, toneStack, cathodeFollower, pad };

struct BlockSpec
{
    BlockKind kind = BlockKind::none;
    int index = 0;      // triode index
    float value = 0.0f; // pad dB
};

struct BrightSpec
{
    bool available = false;
    float hz = 1500.0f;
    float maxDb = 9.0f;
};

struct AmpVoicing
{
    const char* id = "";
    const char* name = "";
    const char* shortDescription = "";
    AmpFamily family = AmpFamily::american;
    const char* gainLabel = "Gain";
    uint32_t controls = 0;

    float inputHpHz = 20.0f;
    std::array<TriodeSpec, 4> triodes {};
    std::array<BlockSpec, 10> chain {};
    ToneStackComponents toneStack;
    CathodeFollowerSpec cathodeFollower;
    BrightSpec bright;
    float gainFloorDb = -40.0f;

    PowerAmpSpec power;
    float powerDriveDb = 0.0f;   // preamp output -> power amp input scaling
    float outputTrimDb = 0.0f;   // level calibration so models are similarly loud
    bool prePowerEQDefault = true;
    const char* defaultIrId = "";
};

constexpr int kNumAmpModels = 6;
const AmpVoicing& getAmpVoicing (int index) noexcept;
int getNumAmpModels() noexcept;

struct AmpControls
{
    float gain = 0.5f, bass = 0.5f, mid = 0.5f, treble = 0.5f;
    float presence = 0.5f, depth = 0.5f, master = 0.5f;
    bool bright = false;
};

class AmpModel
{
public:
    void prepare (double oversampledRate, const AmpVoicing& v);
    void reset() noexcept;

    /** Smoothed control values; call once per control block, before processing it. */
    void setControls (const AmpControls& c);

    /** Preamp: grid-normalised guitar signal in, power-amp-input-scaled signal out. In place. */
    void processPreamp (float* buf, int n) noexcept;
    /** Power amp and output calibration. In place. */
    void processPower (float* buf, int n) noexcept;

    const AmpVoicing& getVoicing() const noexcept { return *voicing; }

    static float potTaper (float knob, float floorDb) noexcept;

private:
    const AmpVoicing* voicing = &getAmpVoicing (0);
    double fs = 192000.0;
    OnePole inputHp;
    std::array<TriodeStage, 4> triodes;
    ToneStack toneStack;
    CathodeFollower follower;
    PowerAmp power;
    Biquad brightShelf;
    bool brightOn = false;

    AmpControls current;
    float gainPotFrom = 0.0f, gainPotTo = 0.0f;
    float masterFrom = 0.0f, masterTo = 0.0f;
    float powerDrive = 1.0f, outputTrim = 1.0f;
    float lastToneB = -1, lastToneM = -1, lastToneT = -1, lastBrightGain = -1;
    bool lastBright = false;
};

} // namespace af::dsp
