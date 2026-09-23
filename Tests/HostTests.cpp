#include <juce_audio_utils/juce_audio_utils.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#endif

namespace
{
constexpr double testSampleRate = 48000.0;
constexpr int maximumBlock = 512;
constexpr double pi = 3.14159265358979323846;

void require (bool condition, const juce::String& message)
{
    if (! condition)
        throw std::runtime_error (message.toStdString());
    std::cout << "PASS: " << message << std::endl;
}

void pumpMessages (int milliseconds)
{
    const double stop = juce::Time::getMillisecondCounterHiRes() + milliseconds;
    do
    {
       #if JUCE_WINDOWS
        MSG message {};
        while (PeekMessage (&message, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage (&message);
            DispatchMessage (&message);
        }
       #elif JUCE_MODAL_LOOPS_PERMITTED
        juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
       #endif
        juce::Thread::sleep (1);
    }
    while (juce::Time::getMillisecondCounterHiRes() < stop);
}

juce::AudioProcessorParameter& parameter (juce::AudioPluginInstance& plugin, const juce::String& name)
{
    for (auto* candidate : plugin.getParameters())
        if (candidate->getName (256).trim().equalsIgnoreCase (name))
            return *candidate;
    throw std::runtime_error (("Hosted parameter missing: " + name).toStdString());
}

void setParameter (juce::AudioPluginInstance& plugin, const juce::String& name, float normalised)
{
    auto& p = parameter (plugin, name);
    p.beginChangeGesture();
    p.setValueNotifyingHost (normalised);
    p.endChangeGesture();
    // Deliver the host/controller synchronisation before the next state query.
    pumpMessages (2);
}

juce::MemoryBlock state (juce::AudioPluginInstance& plugin)
{
    juce::MemoryBlock result;
    plugin.getStateInformation (result);
    if (result.isEmpty())
        throw std::runtime_error ("VST3 returned empty state");
    return result;
}

std::unique_ptr<juce::XmlElement> componentXml (const juce::MemoryBlock& stateBlock)
{
    const auto outer = juce::AudioProcessor::getXmlFromBinary
                       (stateBlock.getData(), static_cast<int> (stateBlock.getSize()));
    if (outer == nullptr || ! outer->hasTagName ("VST3PluginState"))
        throw std::runtime_error ("Missing JUCE VST3 host state envelope");
    const auto* component = outer->getChildByName ("IComponent");
    juce::MemoryBlock binary;
    if (component == nullptr || ! binary.fromBase64Encoding (component->getAllSubText()))
        throw std::runtime_error ("Missing VST3 IComponent state");
    auto result = juce::AudioProcessor::getXmlFromBinary
                  (binary.getData(), static_cast<int> (binary.getSize()));
    if (result == nullptr || ! result->hasTagName ("AutoShapeStereo"))
        throw std::runtime_error ("VST3 component did not expose expected AutoShapeStereo state");
    return result;
}

juce::String learnedValues (const juce::MemoryBlock& stateBlock)
{
    const auto xml = componentXml (stateBlock);
    const auto* learned = xml->getChildByName ("LearnedBands");
    if (learned == nullptr || learned->getNumChildElements() != 8)
        throw std::runtime_error ("Expected eight learned bands in VST3 component state");
    return learned->toString();
}

struct RenderResult
{
    double sidePower = 0.0, outputPower = 0.0;
    float peak = 0.0f;
    bool finite = true;
    std::size_t samples = 0;
};

RenderResult render (juce::AudioPluginInstance& plugin, double seconds, int source = 0)
{
    RenderResult result;
    juce::AudioBuffer<float> buffer (std::max (2, plugin.getTotalNumOutputChannels()), maximumBlock);
    juce::MidiBuffer midi;
    std::uint32_t random = source == 0 ? 0xa1753bd9u : 0x439cb123u;
    const int total = static_cast<int> (std::round (seconds * testSampleRate));
    constexpr int partitions[] { 31, 257, 511, 64, 1, 128 };
    int position = 0;
    std::size_t block = 0;
    while (position < total)
    {
        const int count = std::min (partitions[block++ % 6], total - position);
        buffer.setSize (buffer.getNumChannels(), count, false, false, true);
        buffer.clear();
        for (int n = 0; n < count; ++n)
        {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            const float noise = static_cast<float> (static_cast<double> (random) / 2147483648.0 - 1.0);
            const double time = (position + n) / testSampleRate;
            float value = 0.0f;
            if (source == 0)
                value = 0.10f * noise + 0.035f * static_cast<float> (std::sin (2.0 * pi * 95.0 * time));
            else if (source == 1)
                value = static_cast<float> (0.11 * std::sin (2.0 * pi * 730.0 * time)
                      + 0.045 * std::sin (2.0 * pi * 3300.0 * time))
                      + noise * static_cast<float> (0.07 * std::exp (-32.0 * std::fmod (time, 0.25)));
            buffer.setSample (0, n, value);
            buffer.setSample (1, n, value);
        }

        {
            const juce::ScopedLock callbackLock (plugin.getCallbackLock());
            plugin.processBlock (buffer, midi);
        }
        for (int n = 0; n < count; ++n)
        {
            const float l = buffer.getSample (0, n), r = buffer.getSample (1, n);
            result.finite = result.finite && std::isfinite (l) && std::isfinite (r);
            const double side = 0.5 * (l - r);
            result.sidePower += side * side;
            result.outputPower += 0.5 * (static_cast<double> (l) * l + static_cast<double> (r) * r);
            result.peak = std::max (result.peak, std::max (std::abs (l), std::abs (r)));
            ++result.samples;
        }
        midi.clear();
        position += count;
        if ((block % 100) == 0)
            pumpMessages (1);
    }
    return result;
}

bool captureNativeEditor (juce::AudioProcessorEditor& editor, const juce::File& destination)
{
   #if JUCE_WINDOWS
    // Capture only the test host's own offscreen editor HWND. A normal JUCE
    // component snapshot cannot see the VST3 editor's native child window.
    if (editor.getPeer() == nullptr)
        return false;
    const auto window = static_cast<HWND> (editor.getPeer()->getNativeHandle());
    RECT rectangle {};
    if (! GetClientRect (window, &rectangle))
        return false;
    const int width = rectangle.right - rectangle.left, height = rectangle.bottom - rectangle.top;
    if (width <= 0 || height <= 0 || width > 8000 || height > 8000)
        return false;
    const auto dc = CreateCompatibleDC (nullptr);
    if (dc == nullptr)
        return false;
    BITMAPINFO info {};
    info.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* data = nullptr;
    const auto bitmap = CreateDIBSection (dc, &info, DIB_RGB_COLORS, &data, nullptr, 0);
    if (bitmap == nullptr || data == nullptr)
    {
        if (bitmap != nullptr) DeleteObject (bitmap);
        DeleteDC (dc);
        return false;
    }
    const auto previous = SelectObject (dc, bitmap);
    std::fill_n (static_cast<std::uint32_t*> (data), static_cast<std::size_t> (width) * height, 0xff000000u);
    PrintWindow (window, dc, 2 /* PW_RENDERFULLCONTENT */);
    const auto* pixels = static_cast<const std::uint32_t*> (data);
    bool varies = false;
    for (std::size_t index = 0; index < static_cast<std::size_t> (width) * height; index += 101)
        varies = varies || ((pixels[index] & 0x00ffffffu) != (pixels[0] & 0x00ffffffu));
    bool written = false;
    if (varies)
    {
        juce::Image image (juce::Image::RGB, width, height, true);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                const auto pixel = pixels[static_cast<std::size_t> (y) * width + x];
                image.setPixelAt (x, y, juce::Colour (static_cast<juce::uint8> (pixel >> 16),
                                                     static_cast<juce::uint8> (pixel >> 8),
                                                     static_cast<juce::uint8> (pixel)));
            }
        if (auto stream = destination.createOutputStream())
        {
            stream->setPosition (0);
            stream->truncate();
            written = juce::PNGImageFormat().writeImageToStream (image, *stream);
        }
    }
    SelectObject (dc, previous);
    DeleteObject (bitmap);
    DeleteDC (dc);
    return written;
   #else
    juce::ignoreUnused (editor, destination);
    return false;
   #endif
}

void editorSmoke (juce::AudioPluginInstance& plugin, const juce::File& artifacts, bool writeArtifacts)
{
    require (plugin.hasEditor(), "VST3 advertises its custom editor");
    auto* rawEditor = plugin.createEditorIfNeeded();
    require (rawEditor != nullptr, "VST3 custom editor creates successfully");
    const auto deleter = [&plugin] (juce::AudioProcessorEditor* e)
    {
        e->setVisible (false);
        e->removeFromDesktop();
        plugin.editorBeingDeleted (e);
        delete e;
    };
    std::unique_ptr<juce::AudioProcessorEditor, decltype (deleter)> editor (rawEditor, deleter);
    require (editor->getWidth() >= 600 && editor->getHeight() >= 400,
             "VST3 editor reports usable initial dimensions");
    editor->setTopLeftPosition (-20000, -20000);
    editor->addToDesktop (juce::ComponentPeer::windowIsTemporary
                       | juce::ComponentPeer::windowIgnoresKeyPresses
                       | juce::ComponentPeer::windowIgnoresMouseClicks);
    editor->setVisible (true);
    pumpMessages (150);
    require (editor->getPeer() != nullptr, "VST3 editor attaches to an offscreen native host window");
    if (writeArtifacts)
    {
        const auto screenshot = artifacts.getChildFile ("host-editor.png");
        if (captureNativeEditor (*editor, screenshot))
            std::cout << "Editor snapshot: " << screenshot.getFullPathName() << std::endl;
        else
            std::cout << "INFO: Native-child screenshot unavailable; editor lifecycle checks still completed." << std::endl;
    }
    editor.reset();
    pumpMessages (20);
    require (plugin.getActiveEditor() == nullptr, "VST3 custom editor closes cleanly");
}
} // namespace

int main (int argc, char** argv)
{
    try
    {
        if (argc < 2)
        {
            std::cerr << "Usage: AutoShapeHostTests <Auto Shape Stereo.vst3 bundle> [artifacts directory]" << std::endl;
            return 2;
        }
        juce::ScopedJuceInitialiser_GUI initialise;
        const juce::File bundle (juce::String::fromUTF8 (argv[1]));
        require (bundle.exists(), "Built VST3 bundle exists");
        const bool writeArtifacts = argc > 2;
        const juce::File artifacts = writeArtifacts ? juce::File (juce::String::fromUTF8 (argv[2]))
                                                   : juce::File();
        if (writeArtifacts)
            require (artifacts.createDirectory().wasOk(), "Host test artifact directory is available");

        juce::VST3PluginFormat format;
        juce::OwnedArray<juce::PluginDescription> descriptions;
        format.findAllTypesForFile (descriptions, bundle.getFullPathName());
        require (! descriptions.isEmpty(), "Real VST3 factory scans successfully");
        const juce::PluginDescription* selected = nullptr;
        for (auto* description : descriptions)
        {
            std::cout << "Scanned: " << description->name << " / " << description->manufacturerName
                      << " / " << description->version << std::endl;
            if (description->name.containsIgnoreCase ("Auto Shape"))
                selected = description;
        }
        require (selected != nullptr, "Scanner finds Auto Shape Stereo");
        juce::String error;
        auto plugin = format.createInstanceFromDescription (*selected, testSampleRate, maximumBlock, error);
        require (plugin != nullptr, "Built VST3 instantiates: " + error);
        auto layout = plugin->getBusesLayout();
        require (! layout.inputBuses.isEmpty() && ! layout.outputBuses.isEmpty(), "VST3 exposes audio buses");
        layout.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        require (plugin->setBusesLayout (layout), "VST3 accepts stereo input and output");
        plugin->setRateAndBufferSizeDetails (testSampleRate, maximumBlock);
        plugin->prepareToPlay (testSampleRate, maximumBlock);
        require (plugin->getLatencySamples() == 0, "VST3 reports zero algorithmic latency");

        setParameter (*plugin, "Adapt Speed", 0.0f);
        setParameter (*plugin, "Auto Shape", 0.0f);
        const auto dryRun = render (*plugin, 0.75);
        require (dryRun.finite && dryRun.outputPower > 1.0e-5, "VST3 processes finite non-silent audio");
        require (dryRun.sidePower > dryRun.outputPower * 1.0e-4, "VST3 generates stereo side from identical L/R input");
        const auto initial = state (*plugin);
        const auto initialLearned = learnedValues (initial);

        setParameter (*plugin, "Auto Shape", 1.0f);
        require (parameter (*plugin, "Auto Shape").getValue() > 0.5f, "Host automation starts Auto Shape");
        require (render (*plugin, 0.8).finite, "VST3 remains finite while learning");
        const auto learningFirst = state (*plugin);
        require (learnedValues (learningFirst) != initialLearned, "Learning changes captured band coefficients through real VST3 state");
        require (render (*plugin, 1.2, 1).finite, "VST3 adapts to a changed sound");
        const auto learningSecond = state (*plugin);
        require (learnedValues (learningSecond) != learnedValues (learningFirst), "Learning continues until toggled off");

        setParameter (*plugin, "Auto Shape", 0.0f);
        render (*plugin, 0.05, 2);
        const auto held = state (*plugin);
        require (render (*plugin, 1.5).finite && render (*plugin, 0.5, 1).finite,
                 "Captured processing handles further varied audio");
        const auto afterHeldAudio = state (*plugin);
        require (learnedValues (held) == learnedValues (afterHeldAudio), "Held band coefficients remain exactly unchanged");
        require (held == afterHeldAudio, "Entire VST3 state remains byte-identical while held");

        std::vector<float> savedParameterValues;
        for (auto* p : plugin->getParameters()) savedParameterValues.push_back (p->getValue());
        setParameter (*plugin, "Width", 0.92f);
        setParameter (*plugin, "Auto Shape", 1.0f);
        render (*plugin, 0.5, 1);
        require (state (*plugin) != held, "Host can modify the captured sound before recall");
        plugin->setStateInformation (held.getData(), static_cast<int> (held.getSize()));
        pumpMessages (5);
        render (*plugin, 0.03, 2);
        const auto recalled = state (*plugin);
        require (learnedValues (recalled) == learnedValues (held), "VST3 state recall restores exact learned coefficients");
        require (parameter (*plugin, "Auto Shape").getValue() < 0.5f, "VST3 state recall starts in Held mode");
        bool allParametersRestored = savedParameterValues.size() == static_cast<std::size_t> (plugin->getParameters().size());
        for (int index = 0; index < plugin->getParameters().size() && allParametersRestored; ++index)
            allParametersRestored = std::abs (plugin->getParameters()[index]->getValue()
                                   - savedParameterValues[static_cast<std::size_t> (index)]) < 1.0e-5f;
        require (allParametersRestored, "VST3 state recall restores every host parameter");
        render (*plugin, 0.2, 1);
        require (state (*plugin) == recalled, "Recalled VST3 state remains stable during processing");

        // Also test a new instance, which catches state relying on old DSP history.
        auto fresh = format.createInstanceFromDescription (*selected, testSampleRate, maximumBlock, error);
        require (fresh != nullptr, "A second VST3 instance loads successfully");
        fresh->setStateInformation (held.getData(), static_cast<int> (held.getSize()));
        fresh->setRateAndBufferSizeDetails (testSampleRate, maximumBlock);
        fresh->prepareToPlay (testSampleRate, maximumBlock);
        require (render (*fresh, 0.1).finite, "Fresh VST3 instance processes recalled state");
        require (learnedValues (state (*fresh)) == learnedValues (held), "Saved shape survives a new VST3 instance and prepareToPlay");
        fresh->releaseResources();
        fresh.reset();

        if (writeArtifacts)
        {
            require (artifacts.getChildFile ("held-vst3-state.bin").replaceWithData (held.getData(), held.getSize()),
                     "Captured real VST3 host state saved for inspection");
            require (artifacts.getChildFile ("held-component-state.xml").replaceWithText (componentXml (held)->toString()),
                     "Captured component XML saved for inspection");
        }
        editorSmoke (*plugin, artifacts, writeArtifacts);
        plugin->releaseResources();
        plugin.reset();
        pumpMessages (20);
        std::cout << "ALL REAL-VST3 HOST CONTRACTS PASSED" << std::endl;
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "FAIL: " << exception.what() << std::endl;
        return 1;
    }
}
