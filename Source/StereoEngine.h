#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace autoshape
{
inline constexpr std::size_t bandCount = 8;
// Labels describe broad, overlapping regions, not brick-wall crossover edges.
inline constexpr std::array<float, bandCount> bandFrequencies
    { 50.0f, 120.0f, 300.0f, 750.0f, 1800.0f, 4200.0f, 9000.0f, 16000.0f };

struct Settings
{
    float width = 1.0f;
    float stereoize = 0.65f;
    float lowMonoHz = 180.0f; // Zero bypasses the bass side high-pass.
    float monoSlope = 24.0f; // Continuous blend of 12, 24 and 48 dB/oct filters.
    float transientProtection = 0.6f;
    float centerPreservation = 1.0f;
    float adaptSeconds = 3.0f;
    int character = 1; // 0 tight, 1 natural, 2 diffuse
    bool learning = false;
    std::array<float, bandCount> targetCorrelation
        { 0.995f, 0.98f, 0.90f, 0.72f, 0.48f, 0.26f, 0.31f, 0.38f };
};

struct BandState
{
    // Existing-side gain is 0..3. Synthesis gain is 0..4, giving sparse/coherent
    // sources additional headroom without changing the default shape.
    std::array<float, bandCount> width
        { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    std::array<float, bandCount> generated
        { 0.0f, 0.08f, 0.20f, 0.35f, 0.60f, 0.75f, 0.65f, 0.60f };
};

struct Meter
{
    std::array<float, bandCount> inputCorrelation
        { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    std::array<float, bandCount> outputCorrelation
        { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    std::array<float, bandCount> energy {}; // Input RMS per region, linear amplitude.
    float inputPeak = 0.0f;
    float outputPeak = 0.0f;
};

// Audio-thread owned. The host must publish/copy state outside this class safely.
// No allocation, lock, file access or parameter callbacks occur in process().
class StereoEngine
{
public:
    void prepare (double sampleRate);
    void reset();
    void process (float* left, float* right, int count, const Settings&);
    BandState getBandState() const { return bands; }
    void setBandState (const BandState&);
    Meter getMeter() const;

private:
    struct Splitter
    {
        std::array<float, bandCount - 1> state {};
        std::array<float, bandCount> process (float, const std::array<float, bandCount - 1>&);
    };

    struct Allpass
    {
        std::vector<float> delay;
        std::size_t position = 0;
        float coefficient = 0.5f;
        void prepare (std::size_t length, float gain);
        void reset();
        float process (float);
    };

    struct Highpass
    {
        float integrator1 = 0.0f, integrator2 = 0.0f;
        float process (float input, float g, float damping);
    };

    struct Analysis
    {
        double mid = 0.0, side = 0.0, cross = 0.0;
        double generated = 0.0, sideGenerated = 0.0;
        double outputSide = 0.0, outputCross = 0.0;
    };

    void updateLearning (const Settings&);
    static float correlation (double midPower, double sidePower, double crossPower);

    double rate = 48000.0;
    bool ready = false;
    BandState bands;
    std::array<float, bandCount> currentWidth = bands.width;
    std::array<float, bandCount> currentGenerated = bands.generated;
    std::array<float, bandCount> learningCorrection { 1, 1, 1, 1, 1, 1, 1, 1 };
    std::array<float, bandCount> centerGain { 1, 1, 1, 1, 1, 1, 1, 1 };
    std::array<float, bandCount> currentCenterGain { 1, 1, 1, 1, 1, 1, 1, 1 };
    std::array<float, bandCount - 1> splitCoefficient {};
    Splitter midSplitter, sideSplitter, outputSideSplitter;
    // Three continuously-running characters allow click-free crossfades.
    std::array<std::array<std::array<Allpass, 2>, 3>, bandCount> decorrelators;
    std::array<Highpass, 7> bassFilters;
    std::array<Analysis, bandCount> analysis;
    std::array<float, 3> characterMix { 0.0f, 1.0f, 0.0f };
    float currentGlobalWidth = 1.0f, currentStereoize = 0.65f;
    float currentTransient = 0.6f, currentCenter = 1.0f;
    float currentBassG = 0.0f, currentBassEnable = 1.0f, currentSlope = 24.0f;
    float smoothCoefficient = 0.0f, meterCoefficient = 0.0f;
    float fastAttack = 0.0f, fastRelease = 0.0f, slowCoefficient = 0.0f;
    float peakRelease = 0.0f, fastEnvelope = 0.0f, slowEnvelope = 0.0f;
    float inputPeak = 0.0f, outputPeak = 0.0f, intervalPeak = 0.0f;
    unsigned controlCounter = 0;
    std::size_t samplesSinceReset = 0;
};
} // namespace autoshape
