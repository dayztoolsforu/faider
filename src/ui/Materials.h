#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Drawing textures (ThemeAssets' panel finish, the plugin rack's metal) sharply and tiled.
namespace Materials
{
    inline juce::Colour scaled (juce::Colour c, float v)
    {
        return juce::Colour::fromFloatRGBA (juce::jlimit (0.0f, 1.0f, c.getFloatRed() * v),
                                            juce::jlimit (0.0f, 1.0f, c.getFloatGreen() * v),
                                            juce::jlimit (0.0f, 1.0f, c.getFloatBlue() * v), 1.0f);
    }

    // Brushed aluminium: every brush line a touch lighter or darker, a slow drift along it,
    // and fine grain.
    inline juce::Image makeBrushed (int w, int h, juce::Colour base, bool vertical, int seed)
    {
        juce::Image img (juce::Image::RGB, juce::jmax (1, w), juce::jmax (1, h), false);
        juce::Image::BitmapData px (img, juce::Image::BitmapData::writeOnly);
        juce::Random rng (seed);
        const int across = vertical ? img.getWidth() : img.getHeight();
        const int along = vertical ? img.getHeight() : img.getWidth();

        for (int a = 0; a < across; ++a)
        {
            const float line = (rng.nextFloat() - 0.5f) * 0.09f;
            float drift = 0.0f;
            for (int b = 0; b < along; ++b)
            {
                drift = drift * 0.99f + (rng.nextFloat() - 0.5f) * 0.01f;
                const float v = 1.0f + line + drift + (rng.nextFloat() - 0.5f) * 0.03f;
                px.setPixelColour (vertical ? a : b, vertical ? b : a, scaled (base, v));
            }
        }
        return img;
    }

    // Powder coat / moulded matte finish: a fine stipple, `grain` deep, with the odd catch of light.
    inline juce::Image makeMatte (int w, int h, juce::Colour base, int seed, float grain = 0.07f)
    {
        juce::Image img (juce::Image::RGB, juce::jmax (1, w), juce::jmax (1, h), false);
        juce::Image::BitmapData px (img, juce::Image::BitmapData::writeOnly);
        juce::Random rng (seed);
        for (int y = 0; y < img.getHeight(); ++y)
            for (int x = 0; x < img.getWidth(); ++x)
            {
                float v = 1.0f + (rng.nextFloat() - 0.5f) * grain;
                if (rng.nextInt (90) == 0)
                    v += grain;
                px.setPixelColour (x, y, scaled (base, v));
            }
        return img;
    }

    // How many screen pixels one unit is, right now, in this context (app zoom x Windows display
    // scaling x any transform). Cached images made at this density draw 1:1, so they stay sharp.
    inline float pixelScale (juce::Graphics& g)
    {
        return juce::jlimit (1.0f, 4.0f, std::round (g.getInternalContext().getPhysicalPixelScaleFactor() * 20.0f) / 20.0f);
    }

    // Repeats `img` over `r`. `img` was made at `scale` pixels per unit (see pixelScale).
    inline void tile (juce::Graphics& g, const juce::Image& img, juce::Rectangle<int> r, float scale = 1.0f)
    {
        const float tileW = (float) img.getWidth() / scale, tileH = (float) img.getHeight() / scale;
        for (float y = (float) r.getY(); y < (float) r.getBottom(); y += tileH)
            for (float x = (float) r.getX(); x < (float) r.getRight(); x += tileW)
            {
                const float w = juce::jmin (tileW, (float) r.getRight() - x), h = juce::jmin (tileH, (float) r.getBottom() - y);
                const auto part = img.getClippedImage ({ 0, 0, juce::jmax (1, juce::roundToInt (w * scale)), juce::jmax (1, juce::roundToInt (h * scale)) });
                g.drawImage (part, { x, y, w, h }, juce::RectanglePlacement::stretchToFit);
            }
    }
}
