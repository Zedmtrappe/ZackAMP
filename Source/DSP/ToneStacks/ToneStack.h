// ZackAMP — passive treble/bass/middle ("TMB") tone stack.
//
// The classic passive guitar-amp tone network has a closed-form third-order transfer
// function in terms of its component values and pot positions (see D. T. Yeh and
// J. O. Smith, "Discretization of the '59 Fender Bassman Tone Stack", DAFx 2006).
// We evaluate that analogue transfer function from (original) component sets for each
// amp voicing and discretise it with the bilinear transform. Because the controls are
// the actual pot positions, the knobs interact the way passive tone stacks do: e.g.
// the mid control changes overall level and the bass knob does little with treble at 0.
#pragma once

#include <array>
#include <complex>

namespace af::dsp
{
struct ToneStackComponents
{
    double R1 = 250e3; // treble pot
    double R2 = 1e6;   // bass pot
    double R3 = 25e3;  // mid pot
    double R4 = 56e3;  // slope resistor
    double C1 = 250e-12, C2 = 20e-9, C3 = 20e-9;
};

class ToneStack
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;
    void setComponents (const ToneStackComponents& c);

    /** Knob positions 0..1. Bass uses an audio taper, mid/treble linear (as typical pots). */
    void setControls (double bass, double mid, double treble);

    float process (float x) noexcept
    {
        const double in = x;
        const double y = b[0] * in + z[0];
        z[0] = b[1] * in - a[1] * y + z[1];
        z[1] = b[2] * in - a[2] * y + z[2];
        z[2] = b[3] * in - a[3] * y;
        return (float) y;
    }

    std::complex<double> analogResponse (double freqHz) const;
    std::complex<double> digitalResponse (double freqHz) const;

    /** Computes analogue coefficients (s^0..s^3) for given pot positions (already tapered). */
    static void analogCoefficients (const ToneStackComponents& c, double l, double m, double t,
                                    std::array<double, 4>& num, std::array<double, 4>& den);

    static double bassTaper (double knob);

private:
    double fs = 48000.0;
    ToneStackComponents comp;
    std::array<double, 4> an {}, ad {};          // analogue
    std::array<double, 4> b {}, a { 1, 0, 0, 0 }; // digital
    std::array<double, 3> z {};
};

} // namespace af::dsp
