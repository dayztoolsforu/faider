#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include "Backlight.h"
#include "Theme.h"

// Readouts drawn as a red LED display: numbers in 7-segment digits, names in LED type. Vector
// shapes, so they're sharp at any size. Every readout in the program uses the same screen, so they
// all match.
namespace DotMatrix
{
    inline juce::Colour lit()   { return Theme::get().screenLit; }
    inline juce::Colour glass() { return Theme::get().screenGlass; }
    inline juce::Colour bezel() { return Theme::get().screenBezel; }

    namespace Detail
    {
        // The segments, one bit each: the outline (a top, b c right, d bottom, e f left), the
        // middle bar's halves (g1 g2), the centre verticals (vt vb) and the diagonals (dtl dtr
        // from the top corners to the middle, dbl dbr from the middle to the bottom corners).
        enum : juce::uint16
        {
            a = 1 << 0, b = 1 << 1, c = 1 << 2, d = 1 << 3, e = 1 << 4, f = 1 << 5, g1 = 1 << 6, g2 = 1 << 7,
            vt = 1 << 8, vb = 1 << 9, dtl = 1 << 10, dtr = 1 << 11, dbl = 1 << 12, dbr = 1 << 13,
            g = g1 | g2, outline = a | b | c | d | e | f
        };

        inline juce::uint16 glyph (juce::juce_wchar ch)
        {
            switch (ch)   // the lower-case letters units are written with (dB, kHz, ms, st), as a display shows them
            {
                case 'b': return c | d | e | f | g;             case 'd': return b | c | d | e | g;
                case 'h': return c | e | f | g;                 case 'k': return e | f | g1 | dbr;
                case 'm': return c | e | g | vb;                case 'n': return c | e | g;
                case 'o': return c | d | e | g;                 case 'r': return e | g1;
                case 't': return d | e | f | g1;                case 'u': return c | d | e;
                case 'z': return g1 | dbl | d;
                default: break;
            }
            switch (juce::CharacterFunctions::toUpperCase (ch))
            {
                case '0': return outline;                       case '1': return b | c;
                case '2': return a | b | g | e | d;             case '3': return a | b | g2 | c | d;
                case '4': return f | g | b | c;                 case '5': return a | f | g | c | d;
                case '6': return a | f | e | d | c | g;         case '7': return a | b | c;
                case '8': return outline | g;                   case '9': return a | b | c | d | f | g;
                case 'A': return a | b | c | e | f | g;         case 'B': return a | b | c | d | g2 | vt | vb;
                case 'C': return a | d | e | f;                 case 'D': return a | b | c | d | vt | vb;
                case 'E': return a | d | e | f | g1;            case 'F': return a | e | f | g1;
                case 'G': return a | c | d | e | f | g2;        case 'H': return b | c | e | f | g;
                case 'I': return a | d | vt | vb;               case 'J': return b | c | d | e;
                case 'K': return e | f | g1 | dtr | dbr;        case 'L': return d | e | f;
                case 'M': return b | c | e | f | dtl | dtr;     case 'N': return b | c | e | f | dtl | dbr;
                case 'O': return outline;                       case 'P': return a | b | e | f | g;
                case 'Q': return outline | dbr;                 case 'R': return a | b | e | f | g | dbr;
                case 'S': return a | f | g | c | d;             case 'T': return a | vt | vb;
                case 'U': return b | c | d | e | f;             case 'V': return e | f | dbl | dtr;
                case 'W': return b | c | e | f | dbl | dbr;     case 'X': return dtl | dtr | dbl | dbr;
                case 'Y': return dtl | dtr | vb;                case 'Z': return a | d | dtr | dbl;
                case '-': return g;                             case '_': return d;
                case '+': return g | vt | vb;                   case '=': return g | d;
                case '/': return dtr | dbl;                     case '\\': return dtl | dbr;
                case '*': return g | vt | vb | dtl | dtr | dbl | dbr;
                case '%': return f | c | dtr | dbl;             case '#': return b | c | d | g | vt | vb;
                case '$': return a | f | g | c | d | vt | vb;   case '&': return a | d | e | g1 | dtr | dbr;
                case '@': return a | b | d | e | f | g2 | vt;   case '?': return a | b | g2 | vb;
                case '!': return b | c;                         case '|': return vt | vb;
                case '(': case '<': return dtr | dbr;           case ')': case '>': return dtl | dbl;
                case '[': return a | d | e | f;                 case ']': return a | b | c | d;
                case '\'': return vt;                           case '"': return f | vt;
                case '^': return dbl | dbr;                     case '~': return g1 | g2;
                case 0x221e: return c | d | e | g;              // infinity: a small o
                default: return 0;                              // a space (or a sign it hasn't got)
            }
        }

        // One character position: its segments and decimal point, or a colon (narrower).
        struct Cell { juce::uint16 segments = 0; bool point = false, colon = false; };

        // The text as positions: a '.' or ',' lights the point after the character before it
        // (as on a real display), a ':' is a colon position of its own.
        inline std::vector<Cell> cells (const juce::String& text)
        {
            std::vector<Cell> out;
            for (auto ch : text)
            {
                if ((ch == '.' || ch == ',') && ! out.empty() && ! out.back().point && ! out.back().colon)
                    out.back().point = true;
                else if (ch == '.' || ch == ',')
                    out.push_back ({ 0, true, false });
                else if (ch == ':' || ch == ';')
                    out.push_back ({ 0, false, true });
                else
                    out.push_back ({ glyph (ch), false, false });
            }
            return out;
        }

        // Proportions, as fractions of the characters' height.
        constexpr float widthOf = 0.6f, spaceOf = 0.26f, slant = 0.1f;
        constexpr float heightOf = 0.8f;    // of the screen's glass
        constexpr float shrinkMost = 0.75f; // as small as names are made to fit

        // How tall the characters stand on a screen filling `area`: from its height, but never
        // over `tallest`, so the characters match on every screen.
        constexpr float tallest = 16.0f;
        inline float characterHeight (juce::Rectangle<float> area)
        {
            return juce::jlimit (4.0f, tallest, (area.getHeight() - 6.0f) * heightOf);
        }

        inline float margin (float h) { return h * 0.45f; }
    }

    // How many characters a screen filling `area` shows, shrunk as far as it goes (for shorten).
    inline int charactersFor (juce::Rectangle<float> area)
    {
        const float h = Detail::characterHeight (area) * Detail::shrinkMost;
        const float room = area.getWidth() - 6.0f - 2.0f * Detail::margin (h) - Detail::slant * h;
        return juce::jmax (0, (int) ((room + Detail::spaceOf * h) / ((Detail::widthOf + Detail::spaceOf) * h)));
    }

    // LARGE FORMAT: text on the same red LED display as the 7-segment digits, in bold capitals with
    // the LEDs' glow (names, and anything the segments can't spell).
    inline void ledType (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area)
    {
        const auto red = juce::Colour (0xffff3a24);
        g.setColour (juce::Colour (0xff140807));
        g.fillRoundedRectangle (area.reduced (0.5f), 3.0f);
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (area.reduced (0.5f), 3.0f, 1.0f);
        const auto inner = area.reduced (5.0f, 2.0f).toNearestInt();
        g.setFont (juce::FontOptions (juce::jlimit (8.0f, 22.0f, (float) inner.getHeight() * 0.62f), juce::Font::bold));
        g.setColour (red.withAlpha (0.22f));   // (the glow round the lit letters)
        g.drawFittedText (text, inner.translated (0, 1), juce::Justification::centred, 1, 0.7f);
        g.setColour (red);
        g.drawFittedText (text, inner, juce::Justification::centred, 1, 0.7f);
    }


    // A red 7-segment LED display: bevelled segments, the dark ones still
    // faintly there, the lit ones glowing, a decimal point after a digit. Returns false (nothing
    // drawn) for text a 7-segment display can't spell.
    inline bool sevenSegment (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area)
    {
        // segments: a top, b top right, c bottom right, d bottom, e bottom left, f top left, g middle
        auto maskOf = [] (juce::juce_wchar ch) -> int
        {
            switch (juce::CharacterFunctions::toLowerCase (ch))
            {
                case '0': case 'o': return ch == 'o' ? 0b1011100 : 0b0111111;
                case '1': case 'i': return 0b0000110;
                case '2': return 0b1011011;  case '3': return 0b1001111;  case '4': return 0b1100110;
                case '5': case 's': return 0b1101101;  case '6': return 0b1111101;  case '7': return 0b0000111;
                case '8': return 0b1111111;  case '9': return 0b1101111;
                case 'a': return 0b1110111;  case 'b': return 0b1111100;  case 'c': return 0b0111001;
                case 'd': return 0b1011110;  case 'e': return 0b1111001;  case 'f': return 0b1110001;
                case 'h': return 0b1110110;  case 'l': return 0b0111000;  case 'n': return 0b1010100;
                case 'p': return 0b1110011;  case 'r': return 0b1010000;  case 't': return 0b1111000;
                case 'u': return 0b0111110;  case '-': return 0b1000000;  case '_': return 0b0001000;
                case ' ': return 0;
                default: return -1;
            }
        };
        struct Cell { int mask; bool point; };
        std::vector<Cell> cells;
        for (auto ch : text.trim())
        {
            if (ch == '.' || ch == ',')
            {
                if (cells.empty()) cells.push_back ({ 0, true }); else cells.back().point = true;
                continue;
            }
            const int m = maskOf (ch);
            if (m < 0)
                return false;
            cells.push_back ({ m, false });
        }
        if (cells.empty())
            return false;

        const auto red = juce::Colour (0xffff3a24);
        g.setColour (juce::Colour (0xff140807));
        g.fillRoundedRectangle (area.reduced (0.5f), 3.0f);
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (area.reduced (0.5f), 3.0f, 1.0f);

        auto inner = area.reduced (4.0f, 3.0f);
        float h = inner.getHeight();
        float w = h * 0.5f, gap = h * 0.16f;
        const float total = (float) cells.size() * (w + gap) - gap;
        if (total > inner.getWidth())   // (narrower digits to fit)
        {
            const float k = inner.getWidth() / total;
            w *= k; gap *= k; h = juce::jmin (h, h * juce::jmax (0.7f, k));
        }
        const float t = juce::jmax (1.4f, w * 0.18f);   // segment thickness
        float x = inner.getCentreX() - ((float) cells.size() * (w + gap) - gap) * 0.5f;
        const float y = inner.getCentreY() - h * 0.5f;
        auto segment = [t] (float x1, float y1, float x2, float y2)
        {
            juce::Path p;   // a bevelled bar from (x1, y1) to (x2, y2)
            const bool across = std::abs (y2 - y1) < std::abs (x2 - x1);
            const float half = t * 0.5f;
            if (across)
            {
                p.startNewSubPath (x1, y1); p.lineTo (x1 + half, y1 - half); p.lineTo (x2 - half, y2 - half);
                p.lineTo (x2, y2); p.lineTo (x2 - half, y2 + half); p.lineTo (x1 + half, y1 + half);
            }
            else
            {
                p.startNewSubPath (x1, y1); p.lineTo (x1 + half, y1 + half); p.lineTo (x2 + half, y2 - half);
                p.lineTo (x2, y2); p.lineTo (x2 - half, y2 - half); p.lineTo (x1 - half, y1 + half);
            }
            p.closeSubPath();
            return p;
        };
        for (auto& c : cells)
        {
            const float l = x + t * 0.5f, r = x + w - t * 0.5f, top = y + t * 0.5f, mid = y + h * 0.5f, bot = y + h - t * 0.5f;
            const std::array<juce::Path, 7> segs { segment (l + 1, top, r - 1, top), segment (r, top + 1, r, mid - 1), segment (r, mid + 1, r, bot - 1),
                                                   segment (l + 1, bot, r - 1, bot), segment (l, mid + 1, l, bot - 1), segment (l, top + 1, l, mid - 1),
                                                   segment (l + 1, mid, r - 1, mid) };
            for (int s = 0; s < 7; ++s)
            {
                const bool on = (c.mask >> s) & 1;
                if (on)
                {
                    g.setColour (red.withAlpha (0.25f));
                    g.strokePath (segs[(size_t) s], juce::PathStrokeType (2.2f));   // (its glow)
                }
                g.setColour (on ? red : red.withAlpha (0.1f));
                g.fillPath (segs[(size_t) s]);
            }
            const auto dp = juce::Rectangle<float> (t, t).withCentre ({ x + w + gap * 0.5f, bot });
            g.setColour (c.point ? red : red.withAlpha (0.1f));
            g.fillEllipse (dp);
            x += w + gap;
        }
        return true;
    }

    // A name cut down to `characters` (see charactersFor): split into words at - _ . and spaces,
    // with numbers-only words (catalogue numbers) left out, and the uploader's name before a stock
    // library's catalogue number, then as many whole words as fit; if not even the first fits, as
    // much of it as does.
    inline juce::String shorten (const juce::String& name, int characters)
    {
        auto length = [] (const juce::String& s) { return (int) Detail::cells (s).size(); };
        if (length (name) <= characters)
            return name;
        auto words = juce::StringArray::fromTokens (name, " -_.", {});
        words.removeEmptyStrings();
        if (words.size() >= 3 && words[words.size() - 1].length() >= 5 && words[words.size() - 1].containsOnly ("0123456789"))
            words.remove (0);   // a stock library's "uploader-title-words-123456": the uploader goes first
        for (int i = words.size(); --i >= 0;)
            if (words[i].containsOnly ("0123456789"))
                words.remove (i);
        juce::String out;
        for (auto& w : words)
        {
            const auto next = out.isEmpty() ? w : out + " " + w;
            if (length (next) > characters)
                break;
            out = next;
        }
        if (out.isEmpty() && ! words.isEmpty())
            out = words[0].substring (0, juce::jmax (0, characters));
        return out;
    }

    // Draws a screen filling `area` showing `text` (centred). `painted`: the component being
    // painted, so the screen can line up with the display's real pixels (see below).
    inline void draw (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, const juce::Component* painted = nullptr)
    {
        // Line the screen up with the display's pixels, counted from the window, not the
        // component: at 110-150% a component's corner falls part-way into a pixel, and a different
        // part each time it moves (the window being resized), so its edges would shimmer.
        const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
        juce::Point<float> origin;
        if (painted != nullptr)
            if (auto* top = painted->getTopLevelComponent(); top != nullptr && top != painted)
                origin = top->getLocalPoint (painted, juce::Point<float>());
        auto snapX = [&] (float x) { return std::round ((x + origin.x) * scale) / scale - origin.x; };
        auto snapY = [&] (float y) { return std::round ((y + origin.y) * scale) / scale - origin.y; };
        area = { snapX (area.getX()), snapY (area.getY()),                                // the corner on a pixel,
                 std::round (area.getWidth() * scale) / scale, std::round (area.getHeight() * scale) / scale };   // the size the same wherever it is

        // A red LED display, the same for every readout. Numbers and short codes (dB, OFF, -INF) in
        // 7-segment digits; names in LED type, since most letters can't be spelled in segments and
        // a mix of the two reads as two designs.
        const auto t = text.trim().toUpperCase();
        const bool code = t.containsAnyOf ("0123456789") || t == "OFF" || t == "ON" || t == "-INF" || t == "INF" || t.isEmpty();
        if (! code || ! sevenSegment (g, text, area))
            ledType (g, t, area);
    }

    // Makes a label draw as a screen (see drawReadout).
    inline void markReadout (juce::Label& label)
    {
        label.getProperties().set ("dotReadout", true);
        label.setColour (juce::Label::backgroundColourId, glass());
        label.setColour (juce::Label::outlineColourId, bezel());
        label.setJustificationType (juce::Justification::centred);
    }

    // For a LookAndFeel's drawLabel: a slider's value box, or a marked label, draws as a screen
    // unless it's being typed into. Returns false (draw it normally) for any other label.
    inline bool drawReadout (juce::Graphics& g, juce::Label& label)
    {
        const bool marked = label.getProperties()["dotReadout"];
        if ((dynamic_cast<juce::Slider*> (label.getParentComponent()) == nullptr && ! marked) || label.isBeingEdited())
            return false;

        draw (g, label.getText().trim(), label.getLocalBounds().toFloat(), &label);
        return true;
    }
}
