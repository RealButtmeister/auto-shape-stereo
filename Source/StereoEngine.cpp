#include "StereoEngine.h"

#include <algorithm>
#include <cmath>

namespace autoshape
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr unsigned controlInterval = 64;

float bounded (float value, float low, float high, float fallback)
{
    return std::isfinite (value) ? std::clamp (value, low, high) : fallback;
}

float clean (float value)
{
    return std::abs (value) < 1.0e-20f ? 0.0f : value;
}

float follow (float value, float target, float coefficient)
{
    return clean (value + coefficient * (target - value));
}

float timeCoefficient (double seconds, double sampleRate)
{
    return static_cast<float> (-std::expm1 (-1.0 / (seconds * sampleRate)));
}
} // namespace

std::array<float, bandCount> StereoEngine::Splitter::process
    (float input, const std::array<float, bandCount - 1>& coefficient)
{
    std::array<float, bandCount> result {};
    float previous = 0.0f;
    for (std::size_t b = 0; b + 1 < bandCount; ++b)
    {
        state[b] = follow (state[b], input, coefficient[b]);
        result[b] = state[b] - previous;
        previous = state[b];
    }
    result.back() = input - previous;
    // Telescoping residuals sum to the unfiltered input, including at transients.
    // These broad overlapping bands do not provide linear-phase isolation.
    return result;
}

void StereoEngine::Allpass::prepare (std::size_t length, float gain)
{
    delay.assign (std::max<std::size_t> (length, 1), 0.0f);
    coefficient = gain;
    position = 0;
}

void StereoEngine::Allpass::reset()
{
    std::fill (delay.begin(), delay.end(), 0.0f);
    position = 0;
}

float StereoEngine::Allpass::process (float input)
{
    const float delayed = delay[position];
    const float output = delayed - coefficient * input;
    delay[position] = clean (input + coefficient * output);
    if (++position == delay.size())
        position = 0;
    return output;
}

float StereoEngine::Highpass::process (float input, float g, float damping)
{
    // Topology-preserving state-variable filter. g can move smoothly without
    // replacing recursive coefficients or resetting filter history.
    const float v1 = (integrator1 + g * (input - integrator2))
                   / (1.0f + g * (g + damping));
    const float v2 = integrator2 + g * v1;
    integrator1 = clean (2.0f * v1 - integrator1);
    integrator2 = clean (2.0f * v2 - integrator2);
    return input - damping * v1 - v2;
}

void StereoEngine::prepare (double sampleRate)
{
    rate = std::isfinite (sampleRate) ? std::clamp (sampleRate, 8000.0, 768000.0) : 48000.0;
    for (std::size_t b = 0; b + 1 < bandCount; ++b)
    {
        const double boundary = std::min (std::sqrt (static_cast<double> (bandFrequencies[b])
                                             * bandFrequencies[b + 1]), rate * 0.44);
        splitCoefficient[b] = static_cast<float> (-std::expm1 (-2.0 * pi * boundary / rate));
    }

    for (std::size_t b = 0; b < bandCount; ++b)
    {
        // Unequal allpass delays distribute phase shifts. The dry mid never
        // enters a delay, so the plugin reports zero algorithmic latency.
        // This is static decorrelation: a sustained pure tone cannot exhibit
        // the same image as broadband material at an identical correlation.
        const double bandScale = 0.82 + 0.097 * static_cast<double> (b);
        constexpr std::array<double, 3> firstMs { 0.37, 2.7, 9.1 };
        constexpr std::array<double, 3> secondMs { 0.83, 6.1, 17.3 };
        for (std::size_t c = 0; c < 3; ++c)
        {
            decorrelators[b][c][0].prepare
                (static_cast<std::size_t> (std::round (rate * firstMs[c] * bandScale * 0.001)), 0.47f);
            decorrelators[b][c][1].prepare
                (static_cast<std::size_t> (std::round (rate * secondMs[c] / bandScale * 0.001)), 0.47f);
        }
    }

    smoothCoefficient = timeCoefficient (0.020, rate);
    meterCoefficient = timeCoefficient (0.150, rate);
    fastAttack = timeCoefficient (0.001, rate);
    fastRelease = timeCoefficient (0.012, rate);
    slowCoefficient = timeCoefficient (0.045, rate);
    peakRelease = static_cast<float> (std::exp (-1.0 / (0.5 * rate)));
    ready = true;
    reset();
}

void StereoEngine::reset()
{
    // Learned values intentionally survive transport changes and sample-rate
    // changes. Only signal history and meters are reset.
    midSplitter = {};
    sideSplitter = {};
    outputSideSplitter = {};
    bassFilters = {};
    analysis = {};
    for (auto& band : decorrelators)
        for (auto& character : band)
            for (auto& stage : character)
                stage.reset();
    currentWidth = bands.width;
    currentGenerated = bands.generated;
    learningCorrection.fill (1.0f);
    centerGain.fill (1.0f);
    currentCenterGain.fill (1.0f);
    currentGlobalWidth = 1.0f;
    currentStereoize = 0.65f;
    currentTransient = 0.6f;
    currentCenter = 1.0f;
    currentBassG = static_cast<float> (std::tan (pi * 180.0 / rate));
    currentBassEnable = 1.0f;
    currentSlope = 24.0f;
    characterMix = { 0.0f, 1.0f, 0.0f };
    fastEnvelope = slowEnvelope = inputPeak = outputPeak = intervalPeak = 0.0f;
    controlCounter = 0;
    samplesSinceReset = 0;
}

void StereoEngine::setBandState (const BandState& state)
{
    for (std::size_t b = 0; b < bandCount; ++b)
    {
        bands.width[b] = bounded (state.width[b], 0.0f, 3.0f, 1.0f);
        bands.generated[b] = bounded (state.generated[b], 0.0f, 4.0f, 0.0f);
    }
}

float StereoEngine::correlation (double midPower, double sidePower, double crossPower)
{
    const double sum = midPower + sidePower;
    if (sum < 1.0e-14)
        return 1.0f;
    // L=M+S, R=M-S. Including E[MS] distinguishes a pan from decorrelation.
    const double denominator = std::sqrt (std::max (0.0, sum * sum - 4.0 * crossPower * crossPower));
    if (denominator < sum * 1.0e-5)
        return 0.0f; // Correlation is undefined when one channel is silent.
    return static_cast<float> (std::clamp ((midPower - sidePower) / denominator, -1.0, 1.0));
}

Meter StereoEngine::getMeter() const
{
    Meter result;
    for (std::size_t b = 0; b < bandCount; ++b)
    {
        const auto& a = analysis[b];
        result.inputCorrelation[b] = correlation (a.mid, a.side, a.cross);
        result.outputCorrelation[b] = correlation (a.mid, a.outputSide, a.outputCross);
        result.energy[b] = static_cast<float> (std::sqrt (std::max (0.0, a.mid + a.side)));
    }
    result.inputPeak = inputPeak;
    result.outputPeak = outputPeak;
    return result;
}

void StereoEngine::updateLearning (const Settings& settings)
{
    const float monoHz = bounded (settings.lowMonoHz, 0.0f, 2000.0f, 180.0f);
    const float synthesis = bounded (settings.stereoize, 0.0f, 1.0f, 0.65f);
    const double adapt = bounded (settings.adaptSeconds, 0.1f, 30.0f, 3.0f);
    const float learningCoefficient = static_cast<float> (-std::expm1 (-static_cast<double> (controlInterval) / (adapt * rate)));
    const double feedbackCoefficient = -std::expm1 (-static_cast<double> (controlInterval) / (std::max (0.5, adapt * 2.0) * rate));
    const float protectionCoefficient = static_cast<float> (-std::expm1 (-static_cast<double> (controlInterval) / (0.060 * rate)));
    const bool canLearn = settings.learning && intervalPeak > 1.0e-6f
                         && samplesSinceReset > static_cast<std::size_t> (rate * 0.05);

    for (std::size_t b = 0; b < bandCount; ++b)
    {
        const auto& a = analysis[b];
        if (canLearn && a.mid + a.side > 1.0e-9)
        {
            // This power-ratio target is a robust starting estimate, not a
            // promise that every coherent tone can match a requested meter.
            // Bass protection deliberately wins over a wide target in lows.
            const double f = std::min (static_cast<double> (bandFrequencies[b]), rate * 0.44);
            const double bassWeight = monoHz > 0.0f ? 1.0 / (1.0 + std::pow (monoHz / f, 4.0)) : 1.0;
            const double requested = bounded (settings.targetCorrelation[b], -0.5f, 1.0f, 0.5f);
            const double target = 1.0 - (1.0 - requested) * bassWeight;
            const double targetSide = a.mid * (1.0 - target) / std::max (0.1, 1.0 + target);

            // The synthesis bands overlap, so their individual powers cannot
            // predict the summed result. Close that loop using the actual
            // output analyser, slowly correcting the open-loop allocation.
            // Width remains an artistic multiplier outside the learned curve.
            if (targetSide > 1.0e-11 && currentGlobalWidth > 0.05f
                && samplesSinceReset > static_cast<std::size_t> (rate * 0.3))
            {
                const double targetOutput = targetSide * currentGlobalWidth * currentGlobalWidth;
                const double powerRatio = std::clamp (targetOutput / std::max (a.outputSide, 1.0e-12), 0.125, 8.0);
                const double correction = learningCorrection[b]
                                        * std::exp (0.5 * feedbackCoefficient * std::log (powerRatio));
                learningCorrection[b] = static_cast<float> (std::clamp (correction, 0.20, 4.0));
            }
            const double wantedSide = targetSide * learningCorrection[b] * learningCorrection[b];

            // Existing stereo gets moderate widening first; missing energy is
            // supplied by synthesis. With synthesis off, the full 3x band width
            // range remains available. A mono input keeps its width at unity.
            double wantedWidth = 1.0;
            if (a.side > std::max (a.mid * 1.0e-5, 1.0e-12))
                wantedWidth = std::clamp (std::sqrt (wantedSide / a.side), 0.0, synthesis > 0.001f ? 1.8 : 3.0);

            double wantedGenerated = 0.0;
            if (synthesis > 0.001f && a.generated > 1.0e-12)
            {
                const double remaining = wantedSide - a.side * wantedWidth * wantedWidth;
                if (remaining > 0.0)
                {
                    // Solve Pg*q^2 + 2*w*E[Sg]*q = remaining rather than
                    // assuming the existing and generated sides are unrelated.
                    const double cross = wantedWidth * a.sideGenerated;
                    const double discriminant = cross * cross + a.generated * remaining;
                    const double q = (std::sqrt (std::max (0.0, discriminant)) - cross) / a.generated;
                    // Sparse signals may need more synthesis than broadband
                    // noise. This is a feed-forward gain, so the expanded range
                    // does not change the stability of the allpass filters.
                    wantedGenerated = std::clamp (q / synthesis, 0.0, 4.0);
                }
            }
            bands.width[b] = follow (bands.width[b], static_cast<float> (wantedWidth), learningCoefficient);
            bands.generated[b] = follow (bands.generated[b], static_cast<float> (wantedGenerated), learningCoefficient);
        }

        // Center protection limits excessive side energy, without attenuating
        // or filtering M. It can be disabled for deliberately extreme images.
        // This protective envelope keeps operating after learning is frozen;
        // only the learned curve stops moving, as a compressor still processes
        // audio after its threshold setting has been captured.
        // Measure the summed output rather than independent pre-recombination
        // powers. Otherwise overlap cancellation falsely looks like excess side
        // energy and prevents the learner from reaching safe positive targets.
        const double gain = currentCenterGain[b];
        const double sidePower = a.outputSide / std::max (1.0e-6, gain * gain);
        const double maximumRatio = 12.0 - 11.05 * currentCenter;
        const double maximumPower = (a.mid + 1.0e-9) * maximumRatio;
        const float desiredProtection = currentCenter > 0.001f && sidePower > maximumPower
                                      ? static_cast<float> (std::sqrt (maximumPower / sidePower)) : 1.0f;
        centerGain[b] = follow (centerGain[b], desiredProtection, protectionCoefficient);
    }
    intervalPeak = 0.0f;
}

void StereoEngine::process (float* left, float* right, int count, const Settings& settings)
{
    if (! ready || left == nullptr || right == nullptr || count <= 0)
        return;

    const float width = bounded (settings.width, 0.0f, 3.0f, 1.0f);
    const float synthesis = bounded (settings.stereoize, 0.0f, 1.0f, 0.65f);
    const float transient = bounded (settings.transientProtection, 0.0f, 1.0f, 0.6f);
    const float center = bounded (settings.centerPreservation, 0.0f, 1.0f, 1.0f);
    const float cutoff = bounded (settings.lowMonoHz, 0.0f, 2000.0f, 180.0f);
    const float bassG = static_cast<float> (std::tan (pi * std::max (1.0f, cutoff) / rate));
    const float bassEnabled = cutoff > 0.0f ? 1.0f : 0.0f;
    const float slope = bounded (settings.monoSlope, 12.0f, 48.0f, 24.0f);
    const auto character = static_cast<std::size_t> (std::clamp (settings.character, 0, 2));

    if (samplesSinceReset == 0)
    {
        // A newly prepared/restored sound starts at its real settings. Ramping
        // from constructor defaults would colour the first note of every render.
        // Only subsequent edits/automation need the anti-click smoothing below.
        currentWidth = bands.width;
        currentGenerated = bands.generated;
        currentGlobalWidth = width;
        currentStereoize = synthesis;
        currentTransient = transient;
        currentCenter = center;
        currentBassG = bassG;
        currentBassEnable = bassEnabled;
        currentSlope = slope;
        characterMix.fill (0.0f);
        characterMix[character] = 1.0f;
    }

    for (int sample = 0; sample < count; ++sample)
    {
        // Host corruption cannot poison recursive state. Normal finite audio
        // is linear in level; this guard only bounds pathological >80 dBFS input.
        const float inL = bounded (left[sample], -10000.0f, 10000.0f, 0.0f);
        const float inR = bounded (right[sample], -10000.0f, 10000.0f, 0.0f);
        const float mid = 0.5f * (inL + inR);
        const float side = 0.5f * (inL - inR);
        const float peak = std::max (std::abs (inL), std::abs (inR));
        inputPeak = std::max (peak, inputPeak * peakRelease);
        intervalPeak = std::max (intervalPeak, peak);
        fastEnvelope = follow (fastEnvelope, peak, peak > fastEnvelope ? fastAttack : fastRelease);
        slowEnvelope = follow (slowEnvelope, peak, slowCoefficient);

        currentGlobalWidth = follow (currentGlobalWidth, width, smoothCoefficient);
        currentStereoize = follow (currentStereoize, synthesis, smoothCoefficient);
        currentTransient = follow (currentTransient, transient, smoothCoefficient);
        currentCenter = follow (currentCenter, center, smoothCoefficient);
        currentBassG = follow (currentBassG, bassG, smoothCoefficient);
        currentBassEnable = follow (currentBassEnable, bassEnabled, smoothCoefficient);
        currentSlope = follow (currentSlope, slope, smoothCoefficient);
        for (std::size_t c = 0; c < characterMix.size(); ++c)
            characterMix[c] = follow (characterMix[c], c == character ? 1.0f : 0.0f, smoothCoefficient);

        const float onset = std::clamp ((fastEnvelope / (slowEnvelope + 1.0e-5f) - 1.15f) * 0.45f, 0.0f, 1.0f);
        const float transientGain = 1.0f - 0.90f * currentTransient * onset;
        const auto mids = midSplitter.process (mid, splitCoefficient);
        const auto sides = sideSplitter.process (side, splitCoefficient);
        float shapedSide = 0.0f;

        for (std::size_t b = 0; b < bandCount; ++b)
        {
            float generated = 0.0f;
            for (std::size_t c = 0; c < characterMix.size(); ++c)
            {
                const float ap1 = decorrelators[b][c][0].process (mids[b]);
                const float ap2 = decorrelators[b][c][1].process (mids[b]);
                // Matching allpass feed-through terms cancel in the difference,
                // avoiding an immediate copy of M in S (which mostly pans the
                // signal). Their unequal delayed components create the width.
                // Injecting +S/-S guarantees mono cancellation of synthesis.
                generated += characterMix[c] * (ap1 - ap2);
            }

            currentWidth[b] = follow (currentWidth[b], bands.width[b], smoothCoefficient);
            currentGenerated[b] = follow (currentGenerated[b], bands.generated[b], smoothCoefficient);
            currentCenterGain[b] = follow (currentCenterGain[b], centerGain[b], smoothCoefficient);

            const float learnedWidth = currentWidth[b] > 1.0f
                                     ? 1.0f + (currentWidth[b] - 1.0f) * transientGain : currentWidth[b];
            shapedSide += currentCenterGain[b] * (sides[b] * learnedWidth
                         + generated * currentGenerated[b] * currentStereoize * transientGain);

            auto& a = analysis[b];
            const double m = mids[b], s = sides[b], g = generated;
            a.mid += meterCoefficient * (m * m - a.mid);
            a.side += meterCoefficient * (s * s - a.side);
            a.cross += meterCoefficient * (m * s - a.cross);
            a.generated += meterCoefficient * (g * g - a.generated);
            a.sideGenerated += meterCoefficient * (s * g - a.sideGenerated);
        }
        shapedSide *= currentGlobalWidth;

        // Parallel Butterworth responses share the user cutoff. Blending their
        // outputs makes slope automation smooth (in-between slopes are blends,
        // not literal fractional-order filters). Low-end mono is a roll-off,
        // so a finite cutoff never constitutes a brick-wall promise.
        const float hp12 = bassFilters[0].process (shapedSide, currentBassG, 1.414213562f);
        float hp24 = bassFilters[1].process (shapedSide, currentBassG, 1.847759065f);
        hp24 = bassFilters[2].process (hp24, currentBassG, 0.765366865f);
        float hp48 = bassFilters[3].process (shapedSide, currentBassG, 1.961570561f);
        hp48 = bassFilters[4].process (hp48, currentBassG, 1.662939225f);
        hp48 = bassFilters[5].process (hp48, currentBassG, 1.111140466f);
        hp48 = bassFilters[6].process (hp48, currentBassG, 0.390180644f);
        const float bassSide = currentSlope <= 24.0f
                            ? hp12 + (hp24 - hp12) * ((currentSlope - 12.0f) / 12.0f)
                            : hp24 + (hp48 - hp24) * ((currentSlope - 24.0f) / 24.0f);
        float outSide = shapedSide + currentBassEnable * (bassSide - shapedSide);
        outSide = bounded (outSide, -1000000.0f, 1000000.0f, 0.0f);

        // M remains untouched at every setting. Side manipulation still changes
        // stereo timbre and peak level; callers provide output trim/headroom.
        left[sample] = mid + outSide;
        right[sample] = mid - outSide;
        outputPeak = std::max (std::max (std::abs (left[sample]), std::abs (right[sample])), outputPeak * peakRelease);
        const auto outputSides = outputSideSplitter.process (outSide, splitCoefficient);
        for (std::size_t b = 0; b < bandCount; ++b)
        {
            auto& a = analysis[b];
            const double s = outputSides[b];
            a.outputSide += meterCoefficient * (s * s - a.outputSide);
            a.outputCross += meterCoefficient * (static_cast<double> (mids[b]) * s - a.outputCross);
        }

        ++samplesSinceReset;
        if (++controlCounter == controlInterval)
        {
            controlCounter = 0;
            updateLearning (settings);
        }
    }
}
} // namespace autoshape
