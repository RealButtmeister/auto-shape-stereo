#include "PluginEditor.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
const juce::Colour background { 0xff0d141c };
const juce::Colour panel { 0xff141f2a };
const juce::Colour card { 0xff182631 };
const juce::Colour edge { 0xff293b48 };
const juce::Colour ink { 0xffeeeae1 };
const juce::Colour muted { 0xff91a4b2 };
const juce::Colour cyan { 0xff65dfc3 };
const juce::Colour amber { 0xffecb971 };

juce::Font font (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

juce::String frequencyText (float frequency)
{
    return frequency >= 1000.0f ? juce::String (frequency / 1000.0f, frequency >= 10000.0f ? 0 : 1) + "k"
                               : juce::String (juce::roundToInt (frequency));
}

void drawPanel (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour fill = panel)
{
    g.setColour (fill);
    g.fillRoundedRectangle (bounds, 10.0f);
    g.setColour (edge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 10.0f, 1.0f);
}

class InterfaceLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    InterfaceLookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId, ink);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, cyan.withAlpha (0.25f));
        setColour (juce::Slider::trackColourId, cyan);
        setColour (juce::Slider::backgroundColourId, edge);
        setColour (juce::Slider::thumbColourId, ink);
        setColour (juce::ComboBox::backgroundColourId, card);
        setColour (juce::ComboBox::textColourId, ink);
        setColour (juce::ComboBox::outlineColourId, edge);
        setColour (juce::ComboBox::arrowColourId, muted);
        setColour (juce::PopupMenu::backgroundColourId, card);
        setColour (juce::PopupMenu::textColourId, ink);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, cyan.withAlpha (0.20f));
        setColour (juce::PopupMenu::highlightedTextColourId, ink);
        setColour (juce::TextButton::textColourOffId, ink);
        setColour (juce::TextButton::textColourOnId, background);
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff243847));
        setColour (juce::TooltipWindow::textColourId, ink);
        setColour (juce::TooltipWindow::outlineColourId, edge);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override { return font (13.0f, true); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (13.0f); }
    juce::Font getLabelFont (juce::Label&) override { return font (13.0f); }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour&, bool highlighted, bool down) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        auto fill = button.getToggleState() ? amber : card;
        if (highlighted) fill = fill.brighter (0.10f);
        if (down) fill = fill.darker (0.10f);
        g.setColour (fill);
        g.fillRoundedRectangle (bounds, 6.0f);
        g.setColour (button.hasKeyboardFocus (true) ? cyan : edge);
        g.drawRoundedRectangle (bounds, 6.0f, 1.0f);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float position, float startAngle, float endAngle, juce::Slider& slider) override
    {
        auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (7.0f);
        auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        auto centre = bounds.getCentre();
        auto angle = startAngle + position * (endAngle - startAngle);
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
        g.setColour (edge);
        g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        juce::Path active;
        active.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, angle, true);
        auto colour = slider.findColour (juce::Slider::trackColourId);
        g.setColour (colour.withAlpha (slider.isEnabled() ? 1.0f : 0.3f));
        g.strokePath (active, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        auto inner = juce::Rectangle<float> (radius * 1.5f, radius * 1.5f).withCentre (centre);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff314451), inner.getTopLeft(),
                                                juce::Colour (0xff17252f), inner.getBottomRight(), false));
        g.fillEllipse (inner);
        g.setColour (edge.brighter (0.15f));
        g.drawEllipse (inner, 1.0f);
        const auto pointerStart = centre.getPointOnCircumference (radius * 0.38f, angle);
        const auto pointerEnd = centre.getPointOnCircumference (radius * 0.63f, angle);
        g.setColour (ink);
        g.drawLine ({ pointerStart, pointerEnd }, 2.4f);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float position, float, float, juce::Slider::SliderStyle,
                           juce::Slider& slider) override
    {
        const float cy = (float) y + (float) height * 0.5f;
        g.setColour (edge.brighter (0.12f));
        g.fillRoundedRectangle ((float) x, cy - 2.0f, (float) width, 4.0f, 2.0f);
        g.setColour (slider.findColour (juce::Slider::trackColourId));
        g.fillRoundedRectangle ((float) x, cy - 2.0f, juce::jmax (0.0f, position - (float) x), 4.0f, 2.0f);
        g.setColour (ink);
        g.fillEllipse (position - 4.5f, cy - 4.5f, 9.0f, 9.0f);
    }
};

class AutoShapeButton final : public juce::Button
{
public:
    AutoShapeButton() : juce::Button ("Auto Shape")
    {
        setClickingTogglesState (true);
        setTooltip ("Click to listen and shape continuously. Click again to hold the current settings; audio processing continues.");
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const bool learning = getToggleState();
        auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        auto fill = learning ? cyan : juce::Colour (0xff243a43);
        if (over) fill = fill.brighter (0.10f);
        if (down) fill = fill.darker (0.10f);
        g.setColour (fill);
        g.fillRoundedRectangle (bounds, 9.0f);
        g.setColour (learning ? cyan : cyan.withAlpha (0.65f));
        g.drawRoundedRectangle (bounds, 9.0f, hasKeyboardFocus (true) ? 2.0f : 1.0f);
        g.setColour (learning ? background : ink);
        g.setFont (font (17.0f, true));
        g.drawText ("AUTO SHAPE", 0, 8, getWidth(), 24, juce::Justification::centred);
        g.setFont (font (10.5f, true));
        g.drawText (learning ? "LEARNING  /  CLICK TO HOLD" : "HELD  /  CLICK TO LEARN",
                    0, 32, getWidth(), 16, juce::Justification::centred);
    }
};

class Dial final : public juce::Component
{
public:
    Dial (juce::AudioProcessorValueTreeState& state, const char* parameterId, const char* title,
          const char* detail, const char* tooltip, const char* unit, double multiplier = 1.0,
          int decimals = 0) : name (title), hint (detail)
    {
        addAndMakeVisible (slider);
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                    juce::MathConstants<float>::pi * 2.75f, true);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 110, 23);
        slider.setTooltip (tooltip);
        slider.setName (title);
        slider.setScrollWheelEnabled (false);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, parameterId, slider);
        slider.textFromValueFunction = [unit = juce::String (unit), multiplier, decimals] (double value)
        {
            return juce::String (value * multiplier, decimals) + unit;
        };
        slider.valueFromTextFunction = [multiplier] (const juce::String& value)
        {
            return value.getDoubleValue() / multiplier;
        };
        slider.updateText();
    }

    void paint (juce::Graphics& g) override
    {
        g.setFont (font (11.5f, true));
        g.setColour (ink);
        g.drawText (name, 0, 1, getWidth(), 18, juce::Justification::centred);
        g.setFont (font (10.5f));
        g.setColour (muted);
        g.drawText (hint, 0, getHeight() - 17, getWidth(), 16, juce::Justification::centred);
    }

    void resized() override { slider.setBounds (8, 20, getWidth() - 16, getHeight() - 39); }

    juce::Slider slider;

private:
    juce::String name, hint;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

class BandControls final : public juce::Component
{
public:
    BandControls (AutoShapeAudioProcessor& p, size_t index) : processor (p), band (index)
    {
        for (auto* control : { &width, &generated })
        {
            addAndMakeVisible (*control);
            control->setSliderStyle (juce::Slider::LinearHorizontal);
            control->setTextBoxStyle (juce::Slider::TextBoxRight, false, 43, 22);
            control->setScrollWheelEnabled (false);
            control->textFromValueFunction = [] (double v) { return juce::String (v, 2); };
            control->valueFromTextFunction = [] (const juce::String& text) { return text.getDoubleValue(); };
        }
        width.setRange (0.0, 3.0, 0.01);
        generated.setRange (0.0, 4.0, 0.01);
        width.setDoubleClickReturnValue (true, 1.0);
        generated.setDoubleClickReturnValue (true, 0.0);
        width.setName (frequencyText (autoshape::bandFrequencies[band]) + " Hz band width");
        generated.setName (frequencyText (autoshape::bandFrequencies[band]) + " Hz generated stereo");
        width.setTooltip ("Actual band width: 0 = mono, 1 = original side level, 3 = maximum widening. Editing holds Auto Shape.");
        generated.setTooltip ("Actual generated stereo for this band, from 0 to 4. Editing holds Auto Shape. Global Stereoize scales this amount.");
        generated.setColour (juce::Slider::trackColourId, amber);
        width.onValueChange = [this] { processor.setBandValue (band, false, (float) width.getValue()); };
        generated.onValueChange = [this] { processor.setBandValue (band, true, (float) generated.getValue()); };
        width.onDragStart = [this] { widthDragging = true; };
        width.onDragEnd = [this] { widthDragging = false; };
        generated.onDragStart = [this] { generatedDragging = true; };
        generated.onDragEnd = [this] { generatedDragging = false; };
    }

    void synchronise (float widthValue, float generatedValue)
    {
        if (! widthDragging && ! width.hasKeyboardFocus (true))
            width.setValue (widthValue, juce::dontSendNotification);
        if (! generatedDragging && ! generated.hasKeyboardFocus (true))
            generated.setValue (generatedValue, juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        drawPanel (g, bounds, band % 2 == 0 ? card : panel);
        g.setColour (ink);
        g.setFont (font (14.0f, true));
        g.drawText (frequencyText (autoshape::bandFrequencies[band]) + " Hz", 10, 8,
                    getWidth() - 20, 22, juce::Justification::centredLeft);
        g.setFont (font (9.5f, true));
        g.setColour (muted);
        g.drawText ("WIDTH", 10, 36, getWidth() - 20, 13, juce::Justification::centredLeft);
        g.drawText ("GENERATED", 10, 83, getWidth() - 20, 13, juce::Justification::centredLeft);
    }

    void resized() override
    {
        width.setBounds (8, 49, getWidth() - 16, 25);
        generated.setBounds (8, 96, getWidth() - 16, 25);
    }

private:
    AutoShapeAudioProcessor& processor;
    size_t band;
    juce::Slider width, generated;
    bool widthDragging = false, generatedDragging = false;
};

class CorrelationGraph final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit CorrelationGraph (AutoShapeAudioProcessor& p) : processor (p)
    {
        setWantsKeyboardFocus (true);
        setTooltip ("Drag the amber nodes to choose the target correlation. +1 means matching left and right; 0 means decorrelated; -1 means opposite polarity. Width perception also depends on the source. Arrow keys select and adjust target nodes.");
    }

    ~CorrelationGraph() override
    {
        if (dragging)
            if (auto* parameter = targetParameter ((size_t) selected)) parameter->endChangeGesture();
    }

    void update (const autoshape::Meter& m) { meters = m; repaint(); }

    void paint (juce::Graphics& g) override
    {
        drawPanel (g, getLocalBounds().toFloat());
        g.setColour (ink);
        g.setFont (font (12.0f, true));
        g.drawText ("CORRELATION BY FREQUENCY", 18, 12, 320, 22, juce::Justification::centredLeft);
        g.setFont (font (11.0f));
        g.setColour (muted);
        g.drawText ("Drag amber nodes to set your target", 330, 12, 290, 22, juce::Justification::centredLeft);
        legend (g, getWidth() - 284, "INPUT", muted, false);
        legend (g, getWidth() - 198, "OUTPUT", cyan, false);
        legend (g, getWidth() - 105, "TARGET", amber, true);

        auto plot = plotBounds();
        g.setColour (background.withAlpha (0.7f));
        g.fillRoundedRectangle (plot, 5.0f);
        const auto zeroY = correlationY (0.0f);
        g.setColour (juce::Colour (0xffc76662).withAlpha (0.04f));
        g.fillRect (plot.withTop (zeroY));

        auto* mono = processor.getValueTreeState().getRawParameterValue ("lowMono");
        const float monoFrequency = mono != nullptr ? mono->load() : 0.0f;
        if (monoFrequency >= 20.0f)
        {
            g.setColour (cyan.withAlpha (0.035f));
            g.fillRect (plot.withRight (frequencyX (monoFrequency)));
            g.setColour (cyan.withAlpha (0.3f));
            const auto monoX = frequencyX (monoFrequency);
            const float dashed[] { 3.0f, 4.0f };
            g.drawDashedLine ({ monoX, plot.getY(), monoX, plot.getBottom() }, dashed, 2, 1.0f);
        }

        for (float value : { 1.0f, 0.5f, 0.0f, -0.5f, -1.0f })
        {
            auto y = correlationY (value);
            g.setColour (value == 0.0f ? edge.brighter (0.12f) : edge.withAlpha (0.6f));
            g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
            g.setColour (muted);
            g.setFont (font (10.5f));
            g.drawText ((value > 0.0f ? "+" : "") + juce::String (value, 1),
                        5, juce::roundToInt (y) - 8, 38, 16, juce::Justification::centredRight);
        }

        for (size_t i = 0; i < autoshape::bandCount; ++i)
        {
            const auto x = frequencyX (autoshape::bandFrequencies[i]);
            g.setColour (edge.withAlpha (0.4f));
            g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
            g.setColour (muted);
            g.setFont (font (10.5f));
            g.drawText (frequencyText (autoshape::bandFrequencies[i]),
                        juce::roundToInt (x) - 22, juce::roundToInt (plot.getBottom()) + 5, 44, 16,
                        juce::Justification::centred);
        }

        bool active = false;
        for (auto energy : meters.energy) active = active || energy > 0.000001f;
        if (active)
        {
            auto outputPath = curvePath (meters.outputCorrelation);
            auto fill = outputPath;
            fill.lineTo (frequencyX (autoshape::bandFrequencies.back()), zeroY);
            fill.lineTo (frequencyX (autoshape::bandFrequencies.front()), zeroY);
            fill.closeSubPath();
            g.setColour (cyan.withAlpha (0.08f));
            g.fillPath (fill);
            g.setColour (muted.withAlpha (0.72f));
            g.strokePath (curvePath (meters.inputCorrelation), juce::PathStrokeType (1.5f));
            g.setColour (cyan);
            g.strokePath (outputPath, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved));
        }
        else
        {
            g.setColour (muted.withAlpha (0.55f));
            g.setFont (font (12.0f));
            g.drawText ("Play audio to see the input and output", plot.withHeight (22).withY (zeroY + 17),
                        juce::Justification::centred);
        }

        std::array<float, autoshape::bandCount> targets {};
        for (size_t i = 0; i < targets.size(); ++i) targets[i] = target (i);
        const float dashes[] { 5.0f, 4.0f };
        juce::Path dashed;
        juce::PathStrokeType (1.6f).createDashedStroke (dashed, curvePath (targets), dashes, 2);
        g.setColour (amber.withAlpha (0.9f));
        g.fillPath (dashed);
        for (size_t i = 0; i < autoshape::bandCount; ++i)
        {
            auto point = juce::Point<float> (frequencyX (autoshape::bandFrequencies[i]), correlationY (targets[i]));
            if ((int) i == selected && (isMouseOverOrDragging() || hasKeyboardFocus (false)))
            {
                g.setColour (amber.withAlpha (0.15f));
                g.fillEllipse (juce::Rectangle<float> (21, 21).withCentre (point));
                g.setFont (font (10.0f, true));
                g.setColour (amber);
                const auto textY = point.y < plot.getY() + 24 ? point.y + 10 : point.y - 26;
                g.drawText (juce::String (targets[i], 2), juce::roundToInt (point.x) - 24,
                            juce::roundToInt (textY), 48, 17, juce::Justification::centred);
            }
            g.setColour (panel);
            g.fillEllipse (juce::Rectangle<float> (9, 9).withCentre (point));
            g.setColour (amber);
            g.drawEllipse (juce::Rectangle<float> (9, 9).withCentre (point), 2.0f);
        }

        g.setColour (muted);
        g.setFont (font (10.5f));
        g.drawText ("+1  MATCHED", 18, getHeight() - 23, 140, 16, juce::Justification::centredLeft);
        g.drawText ("0  DECORRELATED", 155, getHeight() - 23, 175, 16, juce::Justification::centredLeft);
        g.drawText ("-1  OPPOSITE", 337, getHeight() - 23, 150, 16, juce::Justification::centredLeft);
        g.drawText ("TARGET IS A GUIDE; SOURCE AND LIMITS DETERMINE THE RESULT", getWidth() - 466,
                    getHeight() - 23, 446, 16, juce::Justification::centredRight);
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (! plotBounds().expanded (8.0f).contains (event.position)) return;
        grabKeyboardFocus();
        selected = closestBand (event.position.x);
        if (auto* parameter = targetParameter ((size_t) selected)) parameter->beginChangeGesture();
        dragging = true;
        editFromMouse (event.position.y);
    }

    void mouseDrag (const juce::MouseEvent& event) override { if (dragging) editFromMouse (event.position.y); }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging)
            if (auto* parameter = targetParameter ((size_t) selected)) parameter->endChangeGesture();
        dragging = false;
    }

    void mouseMove (const juce::MouseEvent& event) override
    {
        if (! dragging) selected = closestBand (event.position.x);
        setMouseCursor (plotBounds().contains (event.position) ? juce::MouseCursor::UpDownResizeCursor
                                                              : juce::MouseCursor::NormalCursor);
        repaint();
    }

    void mouseExit (const juce::MouseEvent&) override { repaint(); }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey)
        {
            selected = juce::jlimit (0, (int) autoshape::bandCount - 1,
                                     selected + (key == juce::KeyPress::leftKey ? -1 : 1));
            repaint();
            return true;
        }
        if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
        {
            if (auto* parameter = targetParameter ((size_t) selected))
            {
                parameter->beginChangeGesture();
                setTarget ((size_t) selected, target ((size_t) selected)
                           + (key == juce::KeyPress::upKey ? 0.01f : -0.01f));
                parameter->endChangeGesture();
                repaint();
            }
            return true;
        }
        return false;
    }

private:
    AutoShapeAudioProcessor& processor;
    autoshape::Meter meters {};
    int selected = 0;
    bool dragging = false;

    juce::Rectangle<float> plotBounds() const
    {
        return { 51.0f, 46.0f, (float) getWidth() - 75.0f, (float) getHeight() - 102.0f };
    }

    float frequencyX (float hz) const
    {
        return plotBounds().getX() + std::log (juce::jlimit (20.0f, 20000.0f, hz) / 20.0f)
             / std::log (1000.0f) * plotBounds().getWidth();
    }

    float correlationY (float value) const
    {
        return plotBounds().getY() + (1.0f - juce::jlimit (-1.0f, 1.0f, value)) * 0.5f * plotBounds().getHeight();
    }

    juce::RangedAudioParameter* targetParameter (size_t i) const
    {
        return processor.getValueTreeState().getParameter ("target" + juce::String ((int) i));
    }

    float target (size_t i) const
    {
        if (auto* parameter = processor.getValueTreeState().getRawParameterValue ("target" + juce::String ((int) i)))
            return parameter->load();
        return 1.0f;
    }

    void setTarget (size_t i, float value)
    {
        if (auto* parameter = targetParameter (i))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (juce::jlimit (0.05f, 1.0f, value)));
    }

    void editFromMouse (float y)
    {
        setTarget ((size_t) selected, 1.0f - (y - plotBounds().getY()) * 2.0f / plotBounds().getHeight());
        repaint();
    }

    int closestBand (float x) const
    {
        int result = 0;
        float distance = std::numeric_limits<float>::max();
        for (size_t i = 0; i < autoshape::bandCount; ++i)
        {
            const auto candidate = std::abs (frequencyX (autoshape::bandFrequencies[i]) - x);
            if (candidate < distance) { result = (int) i; distance = candidate; }
        }
        return result;
    }

    template <typename Values>
    juce::Path curvePath (const Values& values) const
    {
        juce::Path result;
        for (size_t i = 0; i < autoshape::bandCount; ++i)
        {
            const float x = frequencyX (autoshape::bandFrequencies[i]);
            const float y = correlationY (values[i]);
            if (i == 0) result.startNewSubPath (x, y); else result.lineTo (x, y);
        }
        return result;
    }

    void legend (juce::Graphics& g, int x, const char* text, juce::Colour colour, bool node)
    {
        g.setColour (colour);
        if (node) g.drawEllipse ((float) x, 19.0f, 7.0f, 7.0f, 1.5f);
        else g.fillRoundedRectangle ((float) x, 22.0f, 13.0f, 2.0f, 1.0f);
        g.setFont (font (10.0f, true));
        g.drawText (text, x + 19, 12, 68, 22, juce::Justification::centredLeft);
    }
};
}

struct AutoShapeAudioProcessorEditor::Implementation
{
    Implementation (AutoShapeAudioProcessorEditor& e, AutoShapeAudioProcessor& p)
        : editor (e), processor (p), graph (p), tooltip (&e, 550)
    {
        editor.setLookAndFeel (&look);
        auto& state = processor.getValueTreeState();
        editor.addAndMakeVisible (graph);
        editor.addAndMakeVisible (learn);
        learnAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, "autoShape", learn);

        for (auto* button : { &save, &load, &reset, &bypass }) editor.addAndMakeVisible (*button);
        save.setButtonText ("Save");
        load.setButtonText ("Load");
        reset.setButtonText ("Reset shape");
        bypass.setButtonText ("Bypass");
        bypass.setClickingTogglesState (true);
        bypass.setTooltip ("Bypass the effect to compare with the original sound.");
        bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, "bypass", bypass);
        reset.setTooltip ("Stop learning and reset the eight band width/generated settings and target curve. Global controls remain as set.");
        reset.onClick = [this] { processor.resetShape(); refresh(); };
        save.setTooltip ("Save all controls and the held stereo shape to a preset file.");
        load.setTooltip ("Load controls and stereo shape from a preset file.");
        save.onClick = [this] { choosePreset (true); };
        load.onClick = [this] { choosePreset (false); };

        for (size_t i = 0; i < bands.size(); ++i)
        {
            bands[i] = std::make_unique<BandControls> (processor, i);
            editor.addAndMakeVisible (*bands[i]);
        }

        dials[0] = std::make_unique<Dial> (state, "width", "WIDTH", "Overall spread", "Scale both existing and generated side information. 100% uses the current band settings; 0% gives mono.", "%", 100.0);
        dials[1] = std::make_unique<Dial> (state, "stereoize", "STEREOIZE", "Generated detail", "Scale the generated stereo detail. Works on mono sources as well as stereo sources.", "%", 100.0);
        dials[2] = std::make_unique<Dial> (state, "lowMono", "LOW-END MONO", "Bass stays focused", "Roll the side signal down below this frequency with the chosen slope. Set to 0 Hz to turn the low-end mono filter off.", " Hz");
        dials[3] = std::make_unique<Dial> (state, "transient", "TRANSIENTS", "Protect the attack", "Reduce generated stereo around sharp attacks to keep percussion focused.", "%", 100.0);
        dials[4] = std::make_unique<Dial> (state, "center", "CENTER", "Preserve the anchor", "Limit excess side energy to keep the center dominant. The original mid signal is always preserved.", "%", 100.0);
        dials[5] = std::make_unique<Dial> (state, "adaptSeconds", "ADAPT SPEED", "Learning response", "How quickly Auto Shape moves toward the target while learning. Larger values give steadier, slower adjustments.", " s", 1.0, 2);
        dials[6] = std::make_unique<Dial> (state, "mix", "MIX", "Dry / processed", "Blend the original input with the processed stereo result.", "%", 100.0);
        dials[7] = std::make_unique<Dial> (state, "output", "OUTPUT", "Final gain", "Set the final output gain. Leave headroom when adding width and generated stereo.", " dB", 1.0, 1);
        for (auto& dial : dials) editor.addAndMakeVisible (*dial);
        dials[1]->slider.setColour (juce::Slider::trackColourId, amber);

        editor.addAndMakeVisible (character);
        character.addItemList ({ "Tight", "Natural", "Diffuse" }, 1);
        character.setTooltip ("Choose the stereo generation character: tight detail, a natural spread, or a more diffuse texture.");
        characterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, "character", character);
        editor.addAndMakeVisible (slope);
        slope.addItemList ({ "12 dB/oct", "24 dB/oct", "48 dB/oct" }, 1);
        slope.setTooltip ("Choose how sharply side information is rolled off below the Low-end Mono frequency.");
        slopeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, "monoSlope", slope);
        refresh();
    }

    ~Implementation()
    {
        chooser.reset();
        editor.setLookAndFeel (nullptr);
    }

    void refresh()
    {
        auto state = processor.getBandState();
        for (size_t i = 0; i < bands.size(); ++i) bands[i]->synchronise (state.width[i], state.generated[i]);
        meter = processor.getMeters();
        graph.update (meter);
        editor.repaint (24, 84, 835, 24);
        editor.repaint (726, 791, 360, 38);
    }

    void choosePreset (bool saving)
    {
        chooser = std::make_unique<juce::FileChooser> (saving ? "Save Auto Shape Stereo preset" : "Load Auto Shape Stereo preset",
                                                      juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                          .getChildFile ("Auto Shape Stereo.autoshape"), "*.autoshape");
        const auto safeEditor = juce::Component::SafePointer<AutoShapeAudioProcessorEditor> (&editor);
        chooser->launchAsync (saving ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                         | juce::FileBrowserComponent::warnAboutOverwriting
                                    : juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safeEditor, saving] (const juce::FileChooser& selected)
        {
            if (safeEditor == nullptr) return;
            auto selectedFile = selected.getResult();
            if (selectedFile == juce::File()) return;
            if (saving)
            {
                if (! selectedFile.hasFileExtension ("autoshape")) selectedFile = selectedFile.withFileExtension ("autoshape");
                if (! safeEditor->ui->processor.savePreset (selectedFile))
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                           "Preset could not be saved",
                                                           "Choose a writable location and try again.");
            }
            else if (! safeEditor->ui->processor.loadPreset (selectedFile))
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                       "Preset could not be loaded",
                                                       "Choose a valid Auto Shape Stereo preset file.");
            }
            safeEditor->ui->refresh();
        });
    }

    AutoShapeAudioProcessorEditor& editor;
    AutoShapeAudioProcessor& processor;
    InterfaceLookAndFeel look;
    CorrelationGraph graph;
    AutoShapeButton learn;
    juce::TextButton save, load, reset, bypass;
    juce::ComboBox character, slope;
    std::array<std::unique_ptr<BandControls>, autoshape::bandCount> bands;
    std::array<std::unique_ptr<Dial>, 8> dials;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> learnAttachment, bypassAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> characterAttachment, slopeAttachment;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltip;
    autoshape::Meter meter {};
};

AutoShapeAudioProcessorEditor::AutoShapeAudioProcessorEditor (AutoShapeAudioProcessor& processor)
    : AudioProcessorEditor (&processor), ui (std::make_unique<Implementation> (*this, processor))
{
    setResizable (false, false);
    setSize (1120, 860);
    startTimerHz (25);
}

AutoShapeAudioProcessorEditor::~AutoShapeAudioProcessorEditor()
{
    stopTimer();
    ui.reset();
}

void AutoShapeAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.setColour (cyan);
    g.setFont (font (10.0f, true));
    g.drawText ("HONEY BADGER AUDIO", 26, 17, 400, 17, juce::Justification::centredLeft);
    g.setColour (ink);
    g.setFont (font (31.0f, true));
    g.drawText ("Auto Shape Stereo", 24, 36, 540, 39, juce::Justification::centredLeft);
    g.setColour (muted);
    g.setFont (font (12.5f));
    const bool learning = ui->processor.isLearning();
    g.drawText (learning ? "Listening and shaping. Click Auto Shape again to hold the settings."
                         : "Shape held. Press Auto Shape and play audio to begin learning.",
                26, 86, 826, 20, juce::Justification::centredLeft);

    g.setColour (ink);
    g.setFont (font (11.5f, true));
    g.drawText ("THE CURRENT SHAPE", 26, 421, 285, 22, juce::Justification::centredLeft);
    g.setColour (muted);
    g.setFont (font (11.0f));
    g.drawText ("Actual band settings. Edit either row to hold learning and refine the result.",
                301, 421, 793, 22, juce::Justification::centredRight);
    drawPanel (g, { 24.0f, 600.0f, 1072.0f, 165.0f });
    for (int i = 1; i < 8; ++i)
    {
        g.setColour (edge.withAlpha (0.6f));
        g.drawVerticalLine (24 + 134 * i, 622.0f, 745.0f);
    }
    drawPanel (g, { 24.0f, 783.0f, 1072.0f, 53.0f });
    g.setFont (font (10.5f, true));
    g.setColour (muted);
    g.drawText ("CHARACTER", 40, 798, 91, 22, juce::Justification::centredLeft);
    g.drawText ("MONO SLOPE", 282, 798, 102, 22, juce::Justification::centredLeft);

    auto peakText = [] (float value)
    {
        return value < 0.00001f ? juce::String ("-inf") : juce::String (juce::Decibels::gainToDecibels (value), 1);
    };
    g.setColour (muted);
    g.setFont (font (10.0f, true));
    g.drawText ("IN", 754, 791, 120, 17, juce::Justification::centredLeft);
    g.drawText ("OUT", 925, 791, 145, 17, juce::Justification::centredLeft);
    g.setFont (font (13.0f, true));
    g.setColour (ink);
    g.drawText (peakText (ui->meter.inputPeak) + " dBFS", 754, 808, 150, 19, juce::Justification::centredLeft);
    g.setColour (ui->meter.outputPeak >= 1.0f ? amber : cyan);
    g.drawText (peakText (ui->meter.outputPeak) + " dBFS", 925, 808, 151, 19, juce::Justification::centredLeft);
}

void AutoShapeAudioProcessorEditor::resized()
{
    ui->save.setBounds (624, 42, 60, 31);
    ui->load.setBounds (692, 42, 60, 31);
    ui->reset.setBounds (760, 42, 102, 31);
    ui->learn.setBounds (884, 28, 212, 59);
    ui->graph.setBounds (24, 122, 1072, 287);
    for (size_t i = 0; i < ui->bands.size(); ++i)
        ui->bands[i]->setBounds (24 + (int) i * 135, 448, i + 1 == ui->bands.size() ? 127 : 128, 136);
    for (size_t i = 0; i < ui->dials.size(); ++i)
        ui->dials[i]->setBounds (24 + (int) i * 134, 611, 134, 144);
    ui->character.setBounds (137, 795, 117, 29);
    ui->slope.setBounds (389, 795, 128, 29);
    ui->bypass.setBounds (573, 795, 123, 29);
}

void AutoShapeAudioProcessorEditor::timerCallback() { ui->refresh(); }
