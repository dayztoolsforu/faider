#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Cable drawing for the Routing view: a smooth stroke blending from the source's colour to the destination's, brighter
// while audio flows.
namespace CableDraw
{
    // Where a slack cable's middle rests: halfway between its ends, pulled down by "gravity"
    // more the further apart they are.
    inline juce::Point<float> restingSag (juce::Point<float> from, juce::Point<float> to, float droop = 0.32f)
    {
        const float sag = juce::jlimit (36.0f, 190.0f, from.getDistanceFrom (to) * droop);
        return (from + to) * 0.5f + juce::Point<float> (0.0f, sag);
    }

    // A quadratic that passes through `mid` halfway along.
    inline juce::Path hangingPath (juce::Point<float> from, juce::Point<float> to, juce::Point<float> mid)
    {
        juce::Path p;
        p.startNewSubPath (from);
        p.quadraticTo (mid * 2.0f - (from + to) * 0.5f, to);
        return p;
    }

    // One step of a damped spring pulling a cable's middle toward rest; returns true while moving.
    inline bool stepSway (juce::Point<float>& pos, juce::Point<float>& vel, juce::Point<float> rest, float dt)
    {
        constexpr float stiffness = 55.0f, damping = 7.0f;
        const auto accel = (rest - pos) * stiffness - vel * damping;
        vel += accel * dt;
        pos += vel * dt;
        return vel.getDistanceFromOrigin() > 0.5f || pos.getDistanceFrom (rest) > 0.5f;
    }

    // A 1/4" jack plug where a cable meets a jack, seen from behind the rack: its knurled collar
    // seated in the jack at `tip`, the chrome barrel and then the rubber strain-relief boot running
    // out along the cable toward `along` (a point a little way down it), tapering into the cable.
    // `colour`: the cable's (a band of it on the boot).
    inline void drawPlug (juce::Graphics& g, juce::Point<float> tip, juce::Point<float> along, juce::Colour colour, float alpha = 1.0f)
    {
        auto d = along - tip;
        const float len = d.getDistanceFromOrigin();
        if (len < 0.5f || alpha <= 0.0f)
            return;
        d /= len;
        const juce::Point<float> n (-d.y, d.x);   // across the plug

        // A tapered piece from `a` (`wa` across) to `b` (`wb` across), round at both ends.
        auto piece = [&] (float from, float to, float wa, float wb)
        {
            const auto a = tip + d * from, b = tip + d * to;
            juce::Path p;
            p.startNewSubPath (a + n * (wa * 0.5f));
            p.lineTo (b + n * (wb * 0.5f));
            p.lineTo (b - n * (wb * 0.5f));
            p.lineTo (a - n * (wa * 0.5f));
            p.closeSubPath();
            p.addEllipse (juce::Rectangle<float> (wb, wb).withCentre (b));
            p.addEllipse (juce::Rectangle<float> (wa, wa).withCentre (a));
            return p;
        };
        // Metal shaded across its width: bright down the middle where the light catches it.
        auto metal = [&] (float from, float to, juce::Colour edge, juce::Colour shine)
        {
            const auto mid = tip + d * ((from + to) * 0.5f);
            juce::ColourGradient grad (edge.withMultipliedAlpha (alpha), mid - n * 6.0f, edge.withMultipliedAlpha (alpha), mid + n * 6.0f, false);
            grad.addColour (0.42, shine.withMultipliedAlpha (alpha));
            g.setGradientFill (grad);
        };

        // Its shadow on the panel.
        g.setColour (juce::Colours::black.withAlpha (0.45f * alpha));
        g.fillPath (piece (0.0f, 40.0f, 13.0f, 6.0f), juce::AffineTransform::translation (1.2f, 2.4f));

        // The boot: black rubber, tapering into the cable, a band of the cable's colour on it.
        g.setColour (juce::Colour (0xff17181b).withMultipliedAlpha (alpha));
        g.fillPath (piece (20.0f, 40.0f, 11.0f, 5.0f));
        g.setColour (colour.withMultipliedAlpha (0.9f * alpha));
        g.fillPath (piece (23.0f, 26.0f, 10.4f, 9.6f));
        g.setColour (juce::Colours::white.withAlpha (0.10f * alpha));
        g.strokePath (piece (20.0f, 40.0f, 11.0f, 5.0f), juce::PathStrokeType (0.8f));

        // The chrome barrel.
        metal (6.0f, 21.0f, juce::Colour (0xff5d636b), juce::Colour (0xffe9edf1));
        g.fillPath (piece (6.0f, 21.0f, 12.0f, 11.0f));
        g.setColour (juce::Colours::black.withAlpha (0.5f * alpha));
        g.strokePath (piece (6.0f, 21.0f, 12.0f, 11.0f), juce::PathStrokeType (0.8f));

        // The knurled collar seated in the jack, its grip lines across it.
        g.setColour (juce::Colour (0xff2a2d31).withMultipliedAlpha (alpha));
        g.fillEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre (tip));
        metal (-6.0f, 6.0f, juce::Colour (0xff6a7078), juce::Colour (0xffd7dce1));
        g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (tip));
        g.setColour (juce::Colours::black.withAlpha (0.35f * alpha));
        for (float k = -4.5f; k <= 4.6f; k += 1.8f)
            g.drawLine ({ tip + n * k - d * 5.0f, tip + n * k + d * 5.0f }, 0.6f);
        g.setColour (juce::Colours::white.withAlpha (0.35f * alpha));
        g.drawEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (tip), 0.8f);
    }

    // flow 0..1 (audio passing), alpha 0..1 (fading in/out). `thick` cables get a dark outline,
    // like a real patch lead.
    inline void drawFlow (juce::Graphics& g, const juce::Path& path, juce::Point<float> from, juce::Point<float> to,
                          juce::Colour colFrom, juce::Colour colTo, float flow, float alpha,
                          bool dashed, float thickness = 2.5f, bool outlined = false)
    {
        alpha = juce::jlimit (0.0f, 1.0f, alpha);
        if (alpha <= 0.0f)
            return;

        const juce::PathStrokeType stroke (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        if (outlined)
        {
            g.setColour (juce::Colours::black.withAlpha (0.55f * alpha));
            g.strokePath (path, juce::PathStrokeType (thickness + 2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        const float body = alpha * (0.55f + 0.45f * flow);
        g.setGradientFill (juce::ColourGradient (colFrom.withMultipliedAlpha (body), from, colTo.withMultipliedAlpha (body), to, false));

        if (dashed)
        {
            juce::Path d;
            const float dashes[] = { 8.0f, 6.0f };
            stroke.createDashedStroke (d, path, dashes, 2);
            g.fillPath (d);
        }
        else
        {
            g.strokePath (path, stroke);
        }

        if (outlined)   // a soft highlight down the cable's length
        {
            g.setColour (juce::Colours::white.withAlpha (0.12f * alpha));
            g.strokePath (path, juce::PathStrokeType (thickness * 0.35f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                          juce::AffineTransform::translation (0.0f, -thickness * 0.25f));
        }
    }
}
