#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <complex>
#include <juce_dsp/juce_dsp.h>
#include "../engine/VoiceChain.h"
#include "DotMatrix.h"
#include "Console.h"   // Console::Type

// The channel EQ's response at any frequency: the high-pass and the low, mid and high bands as
// the chain has them now (shared by the EQ screens and the Mixer's small EQ graphs).
struct EqResponse
{
    explicit EqResponse (VoiceChain& c)
    {
        auto v = [&c] (const char* id) { return c.params.getRawParameterValue (id)->load(); };
        rate = c.getSampleRate() > 0.0 ? c.getSampleRate() : 48000.0;
        auto hz = [this] (float f) { return juce::jmin ((double) f, rate * 0.45); };
        auto gain = [&v] (const char* id) { return juce::Decibels::decibelsToGain (v (id)); };
        using C = juce::IIRCoefficients;
        hpfOn = v ("hpfOn") > 0.5f;
        eqOn = v ("eqOn") > 0.5f;
        lpfOn = v ("lpfOn") > 0.5f;
        steep = v ("hpfSteep") > 0.5f;
        hpf = C::makeHighPass (rate, v ("hpf"), steep ? VoiceChain::hpfSteepQ1 : VoiceChain::hpfQ);
        hpf2 = C::makeHighPass (rate, v ("hpf"), VoiceChain::hpfSteepQ2);
        lpf = C::makeLowPass (rate, hz (v ("lpf")), VoiceChain::hpfQ);
        low = C::makeLowShelf (rate, hz (v ("lowFreq")), VoiceChain::shelfQ, gain ("lowGain"));
        mid = C::makePeakFilter (rate, hz (v ("midFreq")), v ("midQ"), gain ("midGain"));
        mid2 = C::makePeakFilter (rate, hz (v ("mid2Freq")), v ("mid2Q"), gain ("mid2Gain"));
        high = C::makeHighShelf (rate, hz (v ("highFreq")), VoiceChain::shelfQ, gain ("highGain"));
    }

    // dB at `hz`, with the filters (HPF; LPF while it's in) and/or the bands in.
    double db (double hz, bool withHpf, bool withEq) const
    {
        double gain = 1.0;
        if (withHpf) gain *= magnitude (hpf, hz) * (steep ? magnitude (hpf2, hz) : 1.0);
        if (lpfOn)   gain *= magnitude (lpf, hz);
        if (withEq)  gain *= magnitude (low, hz) * magnitude (mid, hz) * magnitude (mid2, hz) * magnitude (high, hz);
        return juce::Decibels::gainToDecibels (gain, -40.0);
    }

    double rate = 48000.0;
    bool hpfOn = false, eqOn = false, lpfOn = false, steep = false;
    juce::IIRCoefficients hpf, hpf2, lpf, low, mid, mid2, high;

private:
    // |H(e^jw)| of a biquad (JUCE keeps b0 b1 b2 a1 a2, normalised by a0).
    double magnitude (const juce::IIRCoefficients& c, double hz) const
    {
        const auto z1 = std::polar (1.0, -juce::MathConstants<double>::twoPi * hz / rate);
        const auto z2 = z1 * z1;
        const auto num = (double) c.coefficients[0] + (double) c.coefficients[1] * z1 + (double) c.coefficients[2] * z2;
        const auto den = 1.0 + (double) c.coefficients[3] * z1 + (double) c.coefficients[4] * z2;
        return std::abs (num) / juce::jmax (1.0e-12, std::abs (den));
    }
};

// A screen showing the channel EQ's response: the high-pass and the low, mid and high bands together,
// 20 Hz to 20 kHz across, +-12 dB up and down. A stage that's switched out is left out of the
// curve (and the curve dims when both are out). Follows the knobs as they turn. Behind it, the
// channel's live spectrum (what leaves its EQ and dynamics), with a short afterglow.
// Editable (the selected-channel page): larger, +-15 dB, its scales printed, and
// a handle per band to drag: HPF and LPF (frequency), 1 LOW, 2 MID, 3 MID and 4 HIGH (frequency
// and gain; the wheel over a mid sets its Q). Double-click a handle: back to its default.
class EqCurve : public juce::Component,
                private juce::Timer
{
public:
    explicit EqCurve (std::shared_ptr<VoiceChain> c, bool canEdit = false) : chain (std::move (c)), editable (canEdit)
    {
        setInterceptsMouseClicks (editable, false);
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (DotMatrix::bezel());
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (DotMatrix::glass());
        g.fillRoundedRectangle (r.reduced (1.0f), 2.0f);
        const auto screen = screenArea();

        const auto lit = Theme::get().ledGreen;
        const auto spectrumColour = Theme::get().ledBlue;

        // The live spectrum: -78 to 0 dBFS up the whole screen (full scale at the top, so it
        // never runs off it; below -78, hum and hiss nobody hears, so it isn't drawn), filled faintly.
        if (! spectrum.empty())
        {
            juce::Path area;
            area.startNewSubPath (screen.getX(), screen.getBottom());
            for (size_t i = 0; i < spectrum.size(); ++i)
            {
                const float x = screen.getX() + screen.getWidth() * (float) i / (float) (spectrum.size() - 1);
                const float level = juce::jlimit (0.0f, 1.0f, (spectrum[i] + floorDb) / floorDb);
                area.lineTo (x, screen.getBottom() - screen.getHeight() * level);
            }
            area.lineTo (screen.getRight(), screen.getBottom());
            area.closeSubPath();
            g.setColour (spectrumColour.withAlpha (0.35f));
            g.fillPath (area);
        }

        // Grid: 100 Hz, 1 kHz, 10 kHz, and 0 dB (editable: every printed step, with its number).
        const auto grid = Console::Colours::screenInk();
        g.setColour (grid.withAlpha (editable ? 0.18f : 0.12f));
        const auto lines = editable ? std::vector<double> { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 }
                                    : std::vector<double> { 100.0, 1000.0, 10000.0 };
        for (double hz : lines)
            g.fillRect (xOf (hz), screen.getY() + 1.0f, 1.0f, screen.getHeight() - 2.0f);
        g.fillRect (screen.getX() + 1.0f, yOf (0.0), screen.getWidth() - 2.0f, 1.0f);
        if (editable)
        {
            g.setFont (juce::FontOptions (Console::Type::tag));
            for (double db : { 15.0, 10.0, 5.0, -5.0, -10.0, -15.0 })
            {
                g.setColour (grid.withAlpha (0.12f));
                g.fillRect (screen.getX() + 1.0f, yOf (db), screen.getWidth() - 2.0f, 1.0f);
            }
            g.setColour (grid);
            for (double db : { 15.0, 10.0, 5.0, 0.0, -5.0, -10.0, -15.0 })
                g.drawText (juce::String ((int) db), juce::Rectangle<float> (r.getX() + 2.0f, yOf (db) - 6.0f, screen.getX() - r.getX() - 5.0f, 12.0f),
                            juce::Justification::centredRight, false);
            for (double hz : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
                g.drawText (hz >= 1000.0 ? juce::String ((int) (hz / 1000.0)) + "k" : juce::String ((int) hz),
                            juce::Rectangle<float> (xOf (hz) - 16.0f, screen.getBottom() + 2.0f, 32.0f, 12.0f), juce::Justification::centred, false);
        }

        // The response, sampled across the width.
        const EqResponse eq (*chain);
        auto response = [&] (bool withHpf, bool withEq)
        {
            juce::Path path;
            const int steps = juce::jmax (8, (int) screen.getWidth());
            for (int i = 0; i <= steps; ++i)
            {
                const double hz = 20.0 * std::pow (1000.0, (double) i / steps);
                const auto p = juce::Point<float> (xOf (hz), yOf (eq.db (hz, withHpf, withEq)));
                if (i == 0) path.startNewSubPath (p); else path.lineTo (p);
            }
            return path;
        };

        juce::Graphics::ScopedSaveState keep (g);
        g.reduceClipRegion (screen.toNearestInt());

        // What's in (the curve), and faintly, what it would be with every stage in.
        const auto curve = response (eq.hpfOn, eq.eqOn);
        if (! (eq.hpfOn && eq.eqOn))
        {
            g.setColour (lit.withAlpha (0.3f));
            const float dashes[] = { 3.0f, 3.0f };
            juce::Path dashed;
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, response (true, true), dashes, 2);
            g.fillPath (dashed);
        }

        const float alpha = eq.hpfOn || eq.eqOn ? 1.0f : 0.35f;
        juce::Path fill (curve);
        fill.lineTo (screen.getRight(), yOf (0.0));
        fill.lineTo (screen.getX(), yOf (0.0));
        fill.closeSubPath();
        g.setColour (lit.withAlpha (0.15f * alpha));
        g.fillPath (fill);
        g.setColour (lit.withAlpha (alpha));
        g.strokePath (curve, juce::PathStrokeType (editable ? 2.0f : 1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (editable)
            for (int b = 0; b < bandCount; ++b)
            {
                const auto at = handleAt (b);
                const bool on = b == hpfBand ? eq.hpfOn : b == lpfBand ? eq.lpfOn : eq.eqOn;
                const float size = b == dragging || b == hovered ? 22.0f : 18.0f;
                const auto circle = juce::Rectangle<float> (size, size).withCentre (at);
                const auto colour = bandColour (b).withMultipliedAlpha (on ? 1.0f : 0.4f);
                g.setColour (colour.withAlpha (0.35f * (on ? 1.0f : 0.4f)));
                g.fillEllipse (circle.expanded (3.0f));
                g.setColour (colour);
                g.fillEllipse (circle);
                g.setColour (juce::Colours::white.withAlpha (on ? 0.9f : 0.5f));
                g.drawEllipse (circle, 1.2f);
                g.setColour (colour.getPerceivedBrightness() > 0.7f ? juce::Colour (0xff141414) : juce::Colours::white);
                const bool filter = b == hpfBand || b == lpfBand;
                g.setFont (juce::FontOptions (filter ? 8.5f : 11.0f, juce::Font::bold));
                g.drawText (b == hpfBand ? juce::String ("HPF") : b == lpfBand ? juce::String ("LPF") : juce::String (b), circle, juce::Justification::centred, false);
            }
    }

    // The bands' handles and their colours (the band row on the selected-channel page matches).
    // The bands are numbered 1-4 as printed on their handles.
    enum { hpfBand = 0, lowBand = 1, midBand = 2, mid2Band = 3, highBand = 4, lpfBand = 5, bandCount = 6 };
    static juce::Colour bandColour (int band)
    {
        static const juce::Colour colours[] { juce::Colour (0xffd8dce2), juce::Colour (0xffe0524f), juce::Colour (0xffe08a3c),
                                              juce::Colour (0xff4fb3a0), juce::Colour (0xffe0c040), juce::Colour (0xffb0b6c0) };
        return colours[juce::jlimit (0, bandCount - 1, band)];
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int near = bandNear (e.position);
        if (std::exchange (hovered, near) != near)
            repaint();
        setMouseCursor (near >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (std::exchange (hovered, -1) != -1)
            repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = bandNear (e.position);
        for (auto* id : idsOf (dragging))
            if (auto* p = param (id))
                p->beginChangeGesture();
        mouseDrag (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging < 0)
            return;
        const auto screen = screenArea();
        const auto pos = e.position;
        const double hz = 20.0 * std::pow (1000.0, (double) juce::jlimit (0.0f, 1.0f, (pos.x - screen.getX()) / screen.getWidth()));
        const double db = juce::jlimit (-15.0, 15.0, (screen.getCentreY() - pos.y) / (screen.getHeight() * 0.5) * range());
        switch (dragging)
        {
            case hpfBand:  set ("hpf", (float) hz); break;
            case lowBand:  set ("lowFreq", (float) hz); set ("lowGain", (float) db); break;
            case midBand:  set ("midFreq", (float) hz); set ("midGain", (float) db); break;
            case mid2Band: set ("mid2Freq", (float) hz); set ("mid2Gain", (float) db); break;
            case highBand: set ("highFreq", (float) hz); set ("highGain", (float) db); break;
            case lpfBand:  set ("lpf", (float) hz); break;
            default: break;
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        for (auto* id : idsOf (dragging))
            if (auto* p = param (id))
                p->endChangeGesture();
        dragging = -1;
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        for (auto* id : idsOf (bandNear (e.position)))
            if (auto* p = param (id))
                p->setValueNotifyingHost (p->getDefaultValue());
    }

    // The wheel over a mid band sets its Q (anywhere with no band under it: band 2's).
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        const int near = bandNear (e.position);
        if (! editable || (near != midBand && near != mid2Band && near >= 0))
            return;
        if (auto* p = param (near == mid2Band ? "mid2Q" : "midQ"))
            p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, p->getValue() + wheel.deltaY * 0.15f));
    }

private:
    float value (const char* id) const { return chain->params.getRawParameterValue (id)->load(); }
    juce::RangedAudioParameter* param (const char* id) const { return chain->params.getParameter (id); }
    void set (const char* id, float v) { if (auto* p = param (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); }

    double range() const { return editable ? 15.0 : 12.0; }

    // The graph itself: inside the bezel, leaving room for the printed scales when editable.
    juce::Rectangle<float> screenArea() const
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        if (editable)
            r = r.withTrimmedLeft (26.0f).withTrimmedBottom (16.0f).withTrimmedTop (6.0f).withTrimmedRight (8.0f);
        return r;
    }

    float xOf (double hz) const { const auto s = screenArea(); return s.getX() + s.getWidth() * (float) (std::log10 (hz / 20.0) / 3.0); }
    float yOf (double db) const { const auto s = screenArea(); return s.getCentreY() - s.getHeight() * 0.5f * (float) juce::jlimit (-1.0, 1.0, db / range()); }

    juce::Point<float> handleAt (int band) const
    {
        switch (band)
        {
            case hpfBand:  return { xOf (value ("hpf")), yOf (0.0) };
            case lowBand:  return { xOf (value ("lowFreq")), yOf (value ("lowGain")) };
            case midBand:  return { xOf (value ("midFreq")), yOf (value ("midGain")) };
            case mid2Band: return { xOf (value ("mid2Freq")), yOf (value ("mid2Gain")) };
            case highBand: return { xOf (value ("highFreq")), yOf (value ("highGain")) };
            case lpfBand:  return { xOf (value ("lpf")) - 9.0f, yOf (0.0) };   // in from the screen's edge at 20 kHz
            default:       return {};
        }
    }

    int bandNear (juce::Point<float> p) const
    {
        int best = -1;
        float bestDistance = 14.0f;
        for (int b = 0; b < bandCount; ++b)
            if (const float d = handleAt (b).getDistanceFrom (p); d < bestDistance)
            {
                best = b;
                bestDistance = d;
            }
        return best;
    }

    static std::vector<const char*> idsOf (int band)
    {
        switch (band)
        {
            case hpfBand:  return { "hpf" };
            case lowBand:  return { "lowFreq", "lowGain" };
            case midBand:  return { "midFreq", "midGain" };
            case mid2Band: return { "mid2Freq", "mid2Gain" };
            case highBand: return { "highFreq", "highGain" };
            case lpfBand:  return { "lpf" };
            default:       return {};
        }
    }

    // The spectrum each frame (log-spaced columns, falling back slowly); the curve when a setting
    // moves. Not while it can't be seen (the RACK tab, or the Mixer hidden): the FFT is the cost.
    void timerCallback() override
    {
        if (! isShowing())
            return;
        updateSpectrum();
        std::array<float, 17> now {};
        const char* ids[] = { "hpf", "hpfSteep", "hpfOn", "lowGain", "lowFreq", "midGain", "midFreq", "midQ", "mid2Gain", "mid2Freq", "mid2Q",
                              "highGain", "highFreq", "eqOn", "lpf", "lpfOn", "hpf" };
        for (size_t i = 0; i < now.size(); ++i)
            now[i] = value (ids[i]);
        if (now != shown || spectrumMoved)
        {
            shown = now;
            repaint();
        }
    }

    void updateSpectrum()
    {
        const int columns = juce::jmax (8, (int) screenArea().getWidth() / 2);
        if ((int) spectrum.size() != columns)
            spectrum.assign ((size_t) columns, -120.0f);

        chain->copyScope (fft.data());
        std::fill (fft.begin() + VoiceChain::scopeSize, fft.end(), 0.0f);
        window.multiplyWithWindowingTable (fft.data(), (size_t) VoiceChain::scopeSize);
        transform.performFrequencyOnlyForwardTransform (fft.data());

        const double rate = chain->getSampleRate() > 0.0 ? chain->getSampleRate() : 48000.0;
        const int bins = VoiceChain::scopeSize / 2;
        spectrumMoved = false;
        for (int c = 0; c < columns; ++c)
        {
            // The loudest bin under this column (20 Hz to 20 kHz, log-spaced). Low down, where a
            // column is narrower than a bin, the level at its centre between the two nearest bins
            // (so neighbouring columns don't jump together in steps).
            const double lo = 20.0 * std::pow (1000.0, (double) c / columns), hi = 20.0 * std::pow (1000.0, (double) (c + 1) / columns);
            const double binLo = lo / rate * VoiceChain::scopeSize, binHi = hi / rate * VoiceChain::scopeSize;
            float most = 0.0f;
            if (binHi - binLo < 1.0)
            {
                const double at = juce::jlimit (1.0, (double) bins - 2.0, 0.5 * (binLo + binHi));
                const int b = (int) at;
                const float t = (float) (at - b);
                most = fft[(size_t) b] * (1.0f - t) + fft[(size_t) b + 1] * t;
            }
            else
            {
                const int b0 = juce::jlimit (1, bins - 1, (int) binLo);
                const int b1 = juce::jlimit (b0, bins - 1, (int) binHi);
                for (int b = b0; b <= b1; ++b)
                    most = juce::jmax (most, fft[(size_t) b]);
            }
            const float db = juce::Decibels::gainToDecibels (most * 4.0f / (float) VoiceChain::scopeSize, -120.0f);
            auto& shownDb = spectrum[(size_t) c];
            const float next = db > shownDb ? db : shownDb - 1.5f;   // up at once, down about 22 dB a second
            spectrumMoved = spectrumMoved || std::abs (next - shownDb) > 0.2f;
            shownDb = juce::jmax (next, -120.0f);
        }
    }

    static constexpr float floorDb = 78.0f;   // the spectrum's bottom: -78 dBFS
    std::shared_ptr<VoiceChain> chain;
    const bool editable;
    int dragging = -1, hovered = -1;   // editable: the band in hand, the one under the pointer
    std::array<float, 17> shown {};

    juce::dsp::FFT transform { 11 };   // 2048 points (VoiceChain::scopeSize)
    juce::dsp::WindowingFunction<float> window { (size_t) VoiceChain::scopeSize, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, 2 * VoiceChain::scopeSize> fft {};
    std::vector<float> spectrum;   // dB per column, with its fall-back
    bool spectrumMoved = false;
};
