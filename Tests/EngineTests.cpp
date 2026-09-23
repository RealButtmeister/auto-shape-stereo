#include "../Source/StereoEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr std::array<double, 8> analysisFrequencies { 50, 120, 300, 750, 1800, 4200, 9000, 16000 };
const std::vector<int> irregularBlocks { 1, 7, 31, 64, 193, 511, 1024, 17, 4096, 83 };

void require (bool condition, const std::string& message)
{
    if (! condition)
        throw std::runtime_error (message);
}

std::string number (double value)
{
    std::ostringstream stream;
    stream << std::setprecision (7) << value;
    return stream.str();
}

struct Noise
{
    std::uint32_t state = 0x9e3779b9u;

    float next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float> (static_cast<double> (state) / 2147483648.0 - 1.0);
    }
};

struct Audio
{
    std::vector<float> left, right;

    explicit Audio (std::size_t count) : left (count), right (count) {}
    std::size_t size() const { return left.size(); }
};

Audio noiseAudio (double rate, double seconds, bool mono, std::uint32_t seed = 0x12345678u,
                  float amplitude = 0.18f)
{
    Audio result (static_cast<std::size_t> (rate * seconds));
    Noise noise { seed };
    for (std::size_t i = 0; i < result.size(); ++i)
    {
        result.left[i] = noise.next() * amplitude;
        result.right[i] = mono ? result.left[i] : noise.next() * amplitude;
    }
    return result;
}

Audio sineAudio (double rate, double seconds, double frequency, bool antiphase)
{
    Audio result (static_cast<std::size_t> (rate * seconds));
    for (std::size_t i = 0; i < result.size(); ++i)
    {
        result.left[i] = static_cast<float> (0.15 * std::sin (2.0 * pi * frequency * i / rate));
        result.right[i] = antiphase ? -result.left[i] : result.left[i];
    }
    return result;
}

void process (autoshape::StereoEngine& engine, Audio& audio, const autoshape::Settings& settings,
              const std::vector<int>& blocks = irregularBlocks)
{
    std::size_t offset = 0, blockIndex = 0;
    while (offset < audio.size())
    {
        const int count = static_cast<int> (std::min<std::size_t> (
            audio.size() - offset, static_cast<std::size_t> (blocks[blockIndex++ % blocks.size()])));
        engine.process (audio.left.data() + offset, audio.right.data() + offset, count, settings);
        offset += static_cast<std::size_t> (count);
    }
}

bool sameState (const autoshape::BandState& a, const autoshape::BandState& b)
{
    return a.width == b.width && a.generated == b.generated;
}

double stateDistance (const autoshape::BandState& a, const autoshape::BandState& b)
{
    double result = 0;
    for (std::size_t i = 0; i < a.width.size(); ++i)
        result = std::max ({ result, std::abs (static_cast<double> (a.width[i] - b.width[i])),
                           std::abs (static_cast<double> (a.generated[i] - b.generated[i])) });
    return result;
}

void requireFinite (const Audio& audio, const autoshape::StereoEngine& engine)
{
    for (std::size_t i = 0; i < audio.size(); ++i)
    {
        require (std::isfinite (audio.left[i]) && std::isfinite (audio.right[i]),
                 "Non-finite audio at sample " + std::to_string (i));
        require (std::max (std::abs (audio.left[i]), std::abs (audio.right[i])) < 1.0e6f,
                 "Unbounded output at sample " + std::to_string (i));
    }
    const auto state = engine.getBandState();
    const auto meter = engine.getMeter();
    for (std::size_t i = 0; i < state.width.size(); ++i)
    {
        require (std::isfinite (state.width[i]) && std::isfinite (state.generated[i]),
                 "Non-finite learned state");
        require (std::isfinite (meter.inputCorrelation[i]) && std::isfinite (meter.outputCorrelation[i])
                     && std::isfinite (meter.energy[i]), "Non-finite meter state");
        require (std::abs (meter.inputCorrelation[i]) <= 1.001f
                     && std::abs (meter.outputCorrelation[i]) <= 1.001f,
                 "Correlation is outside [-1, 1]");
        require (meter.energy[i] >= 0, "Negative band energy");
    }
    require (std::isfinite (meter.inputPeak) && std::isfinite (meter.outputPeak),
             "Non-finite peak meter");
}

double sideEnergy (const Audio& audio, std::size_t skip)
{
    double result = 0;
    for (std::size_t i = skip; i < audio.size(); ++i)
    {
        const double side = 0.5 * (audio.left[i] - audio.right[i]);
        result += side * side;
    }
    return result / static_cast<double> (audio.size() - skip);
}

double midEnergy (const Audio& audio, std::size_t skip)
{
    double result = 0;
    for (std::size_t i = skip; i < audio.size(); ++i)
    {
        const double mid = 0.5 * (audio.left[i] + audio.right[i]);
        result += mid * mid;
    }
    return result / static_cast<double> (audio.size() - skip);
}

double leftRightPowerRatio (const Audio& audio, std::size_t skip)
{
    double left = 0, right = 0;
    for (std::size_t i = skip; i < audio.size(); ++i)
    {
        left += static_cast<double> (audio.left[i]) * audio.left[i];
        right += static_cast<double> (audio.right[i]) * audio.right[i];
    }
    return left / std::max (1.0e-30, right);
}

// An independent, conventional bandpass analyser verifies rendered audio rather
// than using the engine's meter as the oracle for its own learning algorithm.
struct AnalysisBandpass
{
    double b0, b2, a1, a2;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;

    AnalysisBandpass (double rate, double frequency)
    {
        const double omega = 2.0 * pi * std::min (frequency, rate * 0.45) / rate;
        const double alpha = std::sin (omega) / (2.0 * 1.4);
        const double a0 = 1.0 + alpha;
        b0 = alpha / a0;
        b2 = -b0;
        a1 = -2.0 * std::cos (omega) / a0;
        a2 = (1.0 - alpha) / a0;
    }

    double tick (double x)
    {
        const double y = b0 * x + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }
};

std::array<double, 8> analyseCorrelation (const Audio& audio, double rate, std::size_t skip)
{
    std::array<double, 8> result {};
    for (std::size_t band = 0; band < result.size(); ++band)
    {
        AnalysisBandpass leftFilter (rate, analysisFrequencies[band]);
        AnalysisBandpass rightFilter (rate, analysisFrequencies[band]);
        double sumL = 0, sumR = 0, sumLL = 0, sumRR = 0, sumLR = 0;
        for (std::size_t i = 0; i < audio.size(); ++i)
        {
            const double left = leftFilter.tick (audio.left[i]);
            const double right = rightFilter.tick (audio.right[i]);
            if (i < skip)
                continue;
            sumL += left;
            sumR += right;
            sumLL += left * left;
            sumRR += right * right;
            sumLR += left * right;
        }
        const double count = static_cast<double> (audio.size() - skip);
        const double covariance = sumLR - sumL * sumR / count;
        const double varianceL = sumLL - sumL * sumL / count;
        const double varianceR = sumRR - sumR * sumR / count;
        result[band] = covariance / std::sqrt (std::max (1.0e-30, varianceL * varianceR));
    }
    return result;
}

void testMonoSourceAndMidPreservation()
{
    constexpr double rate = 48000;
    autoshape::StereoEngine engine;
    engine.prepare (rate);
    autoshape::Settings settings;
    settings.learning = false;
    settings.centerPreservation = 1;
    settings.transientProtection = 0;
    auto input = noiseAudio (rate, 2.0, true);
    auto output = input;
    process (engine, output, settings);
    requireFinite (output, engine);

    double maxMidError = 0;
    for (std::size_t i = 0; i < input.size(); ++i)
        maxMidError = std::max (maxMidError, std::abs (
            0.5 * (static_cast<double> (output.left[i]) + output.right[i]) - input.left[i]));
    require (maxMidError < 2.0e-5, "Mono fold-down changed; maximum mid error=" + number (maxMidError));
    const double sideRatio = std::sqrt (sideEnergy (output, 24000) / midEnergy (output, 24000));
    require (sideRatio > 0.01, "Mono source did not acquire meaningful side signal; ratio=" + number (sideRatio));
    const double balance = leftRightPowerRatio (output, 24000);
    require (balance > 0.75 && balance < 1.3334,
             "Stereoizing centered broadband mono caused excessive pan bias; L/R power=" + number (balance));

    auto stereoInput = noiseAudio (rate, 2.0, false);
    auto stereoOutput = stereoInput;
    process (engine, stereoOutput, settings);
    maxMidError = 0;
    for (std::size_t i = 0; i < stereoInput.size(); ++i)
        maxMidError = std::max (maxMidError, std::abs (
            0.5 * (static_cast<double> (stereoOutput.left[i]) + stereoOutput.right[i])
            - 0.5 * (static_cast<double> (stereoInput.left[i]) + stereoInput.right[i])));
    require (maxMidError < 2.0e-5, "Stereo source mid changed; maximum error=" + number (maxMidError));
    std::cout << "  generated side/mid RMS=" << sideRatio << ", maximum mid error=" << maxMidError << '\n';
}

void testWidthZero()
{
    autoshape::StereoEngine engine;
    engine.prepare (48000);
    autoshape::Settings settings;
    settings.width = 0;
    settings.stereoize = 1;
    settings.learning = true;
    auto output = noiseAudio (48000, 2.0, false);
    process (engine, output, settings);
    double maxDifference = 0;
    for (std::size_t i = 24000; i < output.size(); ++i)
        maxDifference = std::max (maxDifference, std::abs (static_cast<double> (output.left[i] - output.right[i])));
    require (maxDifference < 2.0e-5, "Width 0 did not settle to mono; difference=" + number (maxDifference));
}

void testStereoizeZero()
{
    autoshape::StereoEngine engine;
    engine.prepare (48000);
    autoshape::Settings settings;
    settings.stereoize = 0;
    settings.learning = true;
    settings.adaptSeconds = 0.35f;
    auto output = noiseAudio (48000, 3.0, true);
    process (engine, output, settings);
    require (sideEnergy (output, 24000) < 1.0e-12,
             "Stereoize 0 synthesized side from a mono input after control smoothing");
}

void testLearnAndExactFreeze()
{
    constexpr double rate = 48000;
    autoshape::StereoEngine engine;
    engine.prepare (rate);
    autoshape::Settings settings;
    settings.learning = true;
    settings.adaptSeconds = 0.35f;
    const auto initial = engine.getBandState();
    auto training = noiseAudio (rate, 8.0, true);
    process (engine, training, settings);
    const auto learned = engine.getBandState();
    require (stateDistance (initial, learned) > 1.0e-4,
             "Learn did not update settings on audible broadband input");

    settings.learning = false;
    for (int part = 0; part < 6; ++part)
    {
        Audio changing = part == 2 ? Audio (96000)
            : part == 3 ? sineAudio (rate, 2.0, 1400.0, true)
            : noiseAudio (rate, 2.0, (part % 2) == 0, 0x31415926u + static_cast<std::uint32_t> (part));
        // User controls and signal content may change while the learned shape is frozen.
        settings.width = part == 4 ? 0.2f : 1.4f;
        settings.stereoize = part == 5 ? 0.1f : 0.8f;
        settings.targetCorrelation[4] = 0.1f + 0.1f * static_cast<float> (part);
        process (engine, changing, settings);
        require (sameState (learned, engine.getBandState()),
                 "Learn OFF changed stored state during changing audio, part " + std::to_string (part));
        requireFinite (changing, engine);
    }
    std::cout << "  learning changed state by " << stateDistance (initial, learned)
              << "; OFF stayed exactly frozen for 12 seconds\n";
}

void testStatePersistence()
{
    autoshape::StereoEngine engine;
    engine.prepare (48000);
    auto saved = engine.getBandState();
    for (std::size_t i = 0; i < saved.width.size(); ++i)
    {
        saved.width[i] = 0.25f + 0.17f * static_cast<float> (i);
        saved.generated[i] = 0.09f + 0.11f * static_cast<float> (i);
    }
    engine.setBandState (saved);
    require (sameState (saved, engine.getBandState()), "Valid state did not round-trip through setter/getter");
    autoshape::Settings settings;
    settings.learning = false;
    for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        auto audio = noiseAudio (48000, 0.1, false);
        process (engine, audio, settings);
        engine.reset();
        require (sameState (saved, engine.getBandState()), "Reset discarded stored settings");
        engine.prepare (rate);
        require (sameState (saved, engine.getBandState()),
                 "Prepare discarded stored settings at " + number (rate) + " Hz");
    }
}

void testSilenceDoesNotLearn()
{
    autoshape::StereoEngine engine;
    engine.prepare (48000);
    const auto initial = engine.getBandState();
    autoshape::Settings settings;
    settings.learning = true;
    settings.adaptSeconds = 0.1f;
    Audio silence (48000 * 20);
    process (engine, silence, settings);
    requireFinite (silence, engine);
    require (sameState (initial, engine.getBandState()), "Silence moved learned controls");
    require (midEnergy (silence, 0) + sideEnergy (silence, 0) == 0,
             "Silent input created an output signal");

    auto tiny = noiseAudio (48000, 5.0, true, 0x27182818u, 1.0e-12f);
    process (engine, tiny, settings);
    requireFinite (tiny, engine);
    require (sameState (initial, engine.getBandState()), "Inaudible input moved learned controls");
}

void testMonoBass()
{
    constexpr double rate = 48000;
    autoshape::Settings settings;
    settings.learning = false;
    settings.stereoize = 0;
    settings.transientProtection = 0;
    settings.centerPreservation = 0;
    settings.lowMonoHz = 20;
    settings.monoSlope = 24;
    autoshape::StereoEngine openEngine;
    openEngine.prepare (rate);
    auto open = sineAudio (rate, 3.0, 45.0, true);
    process (openEngine, open, settings);

    settings.lowMonoHz = 250;
    autoshape::StereoEngine bassEngine;
    bassEngine.prepare (rate);
    auto bass = sineAudio (rate, 3.0, 45.0, true);
    process (bassEngine, bass, settings);
    const double openEnergy = sideEnergy (open, 48000);
    const double bassEnergy = sideEnergy (bass, 48000);
    require (openEnergy > 1.0e-8, "Low Mono reference unexpectedly removed all 45 Hz side signal");
    const double reductionDb = 10.0 * std::log10 (std::max (1.0e-30, bassEnergy) / openEnergy);
    require (reductionDb < -6.0, "Low Mono failed to suppress bass side by 6 dB; reduction=" + number (reductionDb));
    std::cout << "  Low Mono 250 Hz reduced 45 Hz side by " << -reductionDb << " dB\n";

    std::array<double, 2> slopeEnergies {};
    for (std::size_t i = 0; i < slopeEnergies.size(); ++i)
    {
        autoshape::StereoEngine slopeEngine;
        slopeEngine.prepare (rate);
        settings.monoSlope = i == 0 ? 12.0f : 48.0f;
        auto audio = sineAudio (rate, 3.0, 45.0, true);
        process (slopeEngine, audio, settings);
        slopeEnergies[i] = sideEnergy (audio, 48000);
    }
    require (slopeEnergies[1] < slopeEnergies[0] * 0.1,
             "48 dB/oct Mono Slope did not suppress bass more strongly than 12 dB/oct");
}

void testMetersRecogniseKnownSignals()
{
    for (const bool antiphase : { false, true })
    {
        autoshape::StereoEngine engine;
        engine.prepare (48000);
        autoshape::Settings settings;
        settings.stereoize = 0;
        settings.learning = false;
        auto input = noiseAudio (48000, 2.0, true);
        if (antiphase)
            for (auto& sample : input.right)
                sample = -sample;
        process (engine, input, settings);
        const auto meter = engine.getMeter();
        for (std::size_t band = 2; band < meter.inputCorrelation.size(); ++band)
            require (std::abs (meter.inputCorrelation[band] - (antiphase ? -1.0f : 1.0f)) < 0.02f,
                     "Input correlation meter misread " + std::string (antiphase ? "opposite" : "identical")
                         + " channels in band " + std::to_string (band));
    }
}

void testBlockPartitionIndependence()
{
    autoshape::StereoEngine regularEngine, irregularEngine;
    regularEngine.prepare (48000);
    irregularEngine.prepare (48000);
    autoshape::Settings settings;
    settings.learning = false;
    settings.width = 1.7f;
    settings.stereoize = 0.9f;
    settings.lowMonoHz = 235;
    auto regular = noiseAudio (48000, 2.0, false);
    auto irregular = regular;
    process (regularEngine, regular, settings, { 512 });
    process (irregularEngine, irregular, settings);
    double maxDifference = 0;
    for (std::size_t i = 0; i < regular.size(); ++i)
        maxDifference = std::max ({ maxDifference,
            std::abs (static_cast<double> (regular.left[i] - irregular.left[i])),
            std::abs (static_cast<double> (regular.right[i] - irregular.right[i])) });
    require (maxDifference < 5.0e-6,
             "Sound depends on host block partition; maximum difference=" + number (maxDifference));
}

void testLearningImprovesRenderedShape()
{
    constexpr double rate = 48000;
    autoshape::StereoEngine baselineEngine, learningEngine;
    baselineEngine.prepare (rate);
    learningEngine.prepare (rate);
    auto initial = baselineEngine.getBandState();
    initial.width.fill (1);
    initial.generated.fill (0);
    baselineEngine.setBandState (initial);
    learningEngine.setBandState (initial);
    autoshape::Settings settings;
    settings.stereoize = 1;
    settings.transientProtection = 0;
    settings.adaptSeconds = 0.5f;
    settings.learning = false;
    auto baseline = noiseAudio (rate, 3.0, true, 0x6789abcdu);
    process (baselineEngine, baseline, settings);
    const auto before = analyseCorrelation (baseline, rate, 24000);

    settings.learning = true;
    auto training = noiseAudio (rate, 12.0, true, 0xabcdef01u);
    process (learningEngine, training, settings);
    settings.learning = false;
    const auto frozen = learningEngine.getBandState();
    auto learned = noiseAudio (rate, 3.0, true, 0x6789abcdu);
    process (learningEngine, learned, settings);
    const auto after = analyseCorrelation (learned, rate, 24000);
    require (sameState (frozen, learningEngine.getBandState()), "Measuring frozen shape changed stored state");
    requireFinite (learned, learningEngine);
    const double balance = leftRightPowerRatio (learned, 24000);
    require (balance > 0.75 && balance < 1.3334,
             "Learning introduced excessive broadband pan bias; L/R power=" + number (balance));

    double beforeError = 0, afterError = 0;
    // Mid/high bands are the useful synthetic-width region. The bass has a
    // separate mono constraint, and the topmost band's bandwidth varies with rate.
    for (std::size_t band = 3; band <= 6; ++band)
    {
        beforeError += std::pow (before[band] - settings.targetCorrelation[band], 2);
        afterError += std::pow (after[band] - settings.targetCorrelation[band], 2);
    }
    beforeError /= 4;
    afterError /= 4;
    std::cout << "  independently measured target MSE " << beforeError << " -> " << afterError << "\n  correlations:";
    for (const auto value : after)
        std::cout << ' ' << std::fixed << std::setprecision (3) << value;
    std::cout << "\n  engine meter:";
    for (const auto value : learningEngine.getMeter().outputCorrelation)
        std::cout << ' ' << value;
    std::cout << "\n  held generated gains:";
    for (const auto value : frozen.generated)
        std::cout << ' ' << value;
    std::cout << std::defaultfloat << std::setprecision (6) << '\n';
    require (beforeError > 0.01, "Broadband learning fixture did not begin meaningfully away from target");
    require (afterError < beforeError * 0.8,
             "Learning failed to improve rendered broadband target error by 20%; before="
                 + number (beforeError) + ", after=" + number (afterError));
}

void testRatesAndExtremeControls()
{
    for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        autoshape::StereoEngine engine;
        engine.prepare (rate);
        for (int scenario = 0; scenario < 5; ++scenario)
        {
            if (scenario == 1 || scenario == 4)
            {
                autoshape::BandState maximum;
                maximum.width.fill (3.0f);
                maximum.generated.fill (4.0f);
                engine.setBandState (maximum);
            }
            autoshape::Settings settings;
            settings.learning = true;
            settings.adaptSeconds = scenario == 0 ? -10.0f : scenario == 1 ? 0.0f : 0.1f;
            settings.width = scenario == 0 ? -5.0f : scenario == 1 ? 100.0f : 2.0f;
            settings.stereoize = scenario == 0 ? -5.0f : scenario == 1 ? 100.0f : 1.0f;
            settings.lowMonoHz = scenario == 0 ? -100.0f : scenario == 1 ? 100000.0f : 350.0f;
            settings.monoSlope = scenario == 0 ? -48.0f : scenario == 1 ? 1000.0f : 48.0f;
            settings.transientProtection = scenario == 0 ? -10.0f : scenario == 1 ? 100.0f : 1.0f;
            settings.centerPreservation = scenario == 0 ? -10.0f : scenario == 1 ? 100.0f : 1.0f;
            settings.character = scenario == 0 ? -10 : scenario == 1 ? 100 : scenario - 2;
            if (scenario < 2)
                settings.targetCorrelation.fill (scenario == 0 ? -5.0f : 5.0f);
            auto audio = noiseAudio (rate, 0.4, scenario % 2 == 0,
                                    0x10203040u + static_cast<std::uint32_t> (scenario), scenario == 1 ? 8.0f : 0.15f);
            // Sharp bursts and subnormal inputs exercise detector/filter recovery.
            for (std::size_t i = 0; i < audio.size(); i += 997)
            {
                audio.left[i] = scenario == 2 ? std::numeric_limits<float>::denorm_min() : 2.0f;
                audio.right[i] = -audio.left[i];
            }
            process (engine, audio, settings);
            requireFinite (audio, engine);
        }
    }
}
} // namespace

int main()
{
    const std::vector<std::pair<const char*, std::function<void()>>> tests {
        { "mono source gains stereo while original mid is preserved", testMonoSourceAndMidPreservation },
        { "Width 0 settles to mono", testWidthZero },
        { "Stereoize 0 leaves mono sources mono", testStereoizeZero },
        { "Learn updates and OFF freezes settings exactly", testLearnAndExactFreeze },
        { "stored settings survive reset and sample-rate changes", testStatePersistence },
        { "silent and inaudible input do not train", testSilenceDoesNotLearn },
        { "Low Mono suppresses low-frequency side energy", testMonoBass },
        { "meters recognise known channel relationships", testMetersRecogniseKnownSignals },
        { "frozen sound is independent of host block partition", testBlockPartitionIndependence },
        { "learning improves independently measured broadband shape", testLearningImprovesRenderedShape },
        { "audio stays finite across sample rates and extreme controls", testRatesAndExtremeControls },
    };
    int failures = 0;
    for (const auto& test : tests)
    {
        std::cout << "RUN  " << test.first << '\n';
        try
        {
            test.second();
            std::cout << "PASS " << test.first << '\n';
        }
        catch (const std::exception& error)
        {
            ++failures;
            std::cout << "FAIL " << test.first << ": " << error.what() << '\n';
        }
    }
    std::cout << '\n' << tests.size() - static_cast<std::size_t> (failures) << '/' << tests.size()
              << " tests passed\n";
    return failures == 0 ? 0 : 1;
}
