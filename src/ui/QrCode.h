#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <vector>

// A QR code for a short text (a link): byte mode, error correction level M (15% damage
// recovered), the smallest version 1 to 10 it fits (up to 213 bytes), the mask that scores best.
// Written from the QR code standard (ISO/IEC 18004); tests/QrTest.cpp checks one decodes.
class QrCode
{
public:
    // An empty code (size 0) if the text doesn't fit in version 10.
    static QrCode encode (const juce::String& text)
    {
        const auto utf8 = text.toUTF8();
        const std::vector<juce::uint8> data ((const juce::uint8*) utf8.getAddress(), (const juce::uint8*) utf8.getAddress() + std::strlen (utf8));
        for (int v = 1; v <= 10; ++v)
            if (4 + (v < 10 ? 8 : 16) + 8 * (int) data.size() <= dataCodewords (v) * 8)
                return QrCode (v, data);
        return {};
    }

    int getSize() const { return size; }
    int getVersion() const { return version; }
    bool isDark (int x, int y) const { return juce::isPositiveAndBelow (x, size) && juce::isPositiveAndBelow (y, size) && modules[(size_t) (y * size + x)]; }

private:
    QrCode() = default;

    // Error correction level M, per version: EC codewords per block, then the blocks as
    // (count, data codewords) for the first group and the second.
    struct Layout { int ecPerBlock, count1, data1, count2, data2; };
    static Layout layout (int v)
    {
        static const Layout table[] = { { 10, 1, 16, 0, 0 }, { 16, 1, 28, 0, 0 }, { 26, 1, 44, 0, 0 }, { 18, 2, 32, 0, 0 }, { 24, 2, 43, 0, 0 },
                                        { 16, 4, 27, 0, 0 }, { 18, 4, 31, 0, 0 }, { 22, 2, 38, 2, 39 }, { 22, 3, 36, 2, 37 }, { 26, 4, 43, 1, 44 } };
        return table[v - 1];
    }
    static int dataCodewords (int v) { const auto l = layout (v); return l.count1 * l.data1 + l.count2 * l.data2; }

    static std::vector<int> alignmentPositions (int v)
    {
        static const std::vector<int> table[] = { {}, { 6, 18 }, { 6, 22 }, { 6, 26 }, { 6, 30 }, { 6, 34 },
                                                  { 6, 22, 38 }, { 6, 24, 42 }, { 6, 26, 46 }, { 6, 28, 50 } };
        return table[v - 1];
    }

    // GF(256) with the QR polynomial x^8 + x^4 + x^3 + x^2 + 1.
    static juce::uint8 multiply (juce::uint8 a, juce::uint8 b)
    {
        int product = 0;
        for (int i = 7; i >= 0; --i)
        {
            product = (product << 1) ^ ((product >> 7) * 0x11D);
            product ^= ((b >> i) & 1) * a;
        }
        return (juce::uint8) product;
    }

    // Reed-Solomon: the `degree` check codewords for `data`.
    static std::vector<juce::uint8> checkCodewords (const std::vector<juce::uint8>& data, int degree)
    {
        std::vector<juce::uint8> divisor ((size_t) degree, 0);
        divisor.back() = 1;
        juce::uint8 root = 1;
        for (int i = 0; i < degree; ++i)
        {
            for (size_t j = 0; j < divisor.size(); ++j)
            {
                divisor[j] = multiply (divisor[j], root);
                if (j + 1 < divisor.size())
                    divisor[j] ^= divisor[j + 1];
            }
            root = multiply (root, 0x02);
        }
        std::vector<juce::uint8> result ((size_t) degree, 0);
        for (auto b : data)
        {
            const auto factor = (juce::uint8) (b ^ result.front());
            result.erase (result.begin());
            result.push_back (0);
            for (size_t i = 0; i < result.size(); ++i)
                result[i] ^= multiply (divisor[i], factor);
        }
        return result;
    }

    QrCode (int v, const std::vector<juce::uint8>& text) : version (v), size (v * 4 + 17)
    {
        modules.assign ((size_t) (size * size), false);
        function.assign ((size_t) (size * size), false);

        // The bit stream: byte mode, the length, the text, a terminator, then pad bytes.
        std::vector<bool> bits;
        auto append = [&bits] (int value, int count) { for (int i = count - 1; i >= 0; --i) bits.push_back (((value >> i) & 1) != 0); };
        append (0x4, 4);
        append ((int) text.size(), v < 10 ? 8 : 16);
        for (auto b : text)
            append (b, 8);
        const int capacity = dataCodewords (v) * 8;
        append (0, juce::jmin (4, capacity - (int) bits.size()));
        append (0, (8 - (int) bits.size() % 8) % 8);
        for (int pad = 0xEC; (int) bits.size() < capacity; pad ^= 0xEC ^ 0x11)
            append (pad, 8);
        std::vector<juce::uint8> codewords ((size_t) (capacity / 8), 0);
        for (size_t i = 0; i < bits.size(); ++i)
            codewords[i >> 3] |= (juce::uint8) ((bits[i] ? 1 : 0) << (7 - (i & 7)));

        // Split into blocks, each with its check codewords, then interleaved.
        const auto l = layout (v);
        std::vector<std::vector<juce::uint8>> blocks, checks;
        size_t at = 0;
        for (int g = 0; g < 2; ++g)
            for (int b = 0; b < (g == 0 ? l.count1 : l.count2); ++b)
            {
                const int length = g == 0 ? l.data1 : l.data2;
                blocks.emplace_back (codewords.begin() + (long) at, codewords.begin() + (long) at + length);
                at += (size_t) length;
                checks.push_back (checkCodewords (blocks.back(), l.ecPerBlock));
            }
        std::vector<juce::uint8> stream;
        const int longest = juce::jmax (l.data1, l.data2);
        for (int i = 0; i < longest; ++i)
            for (auto& block : blocks)
                if (i < (int) block.size())
                    stream.push_back (block[(size_t) i]);
        for (int i = 0; i < l.ecPerBlock; ++i)
            for (auto& check : checks)
                stream.push_back (check[(size_t) i]);

        drawFunctionPatterns();
        drawCodewords (stream);

        // The mask with the lowest penalty.
        int best = 0;
        long bestScore = -1;
        for (int mask = 0; mask < 8; ++mask)
        {
            applyMask (mask);
            drawFormatBits (mask);
            const long score = penalty();
            if (bestScore < 0 || score < bestScore)
            {
                best = mask;
                bestScore = score;
            }
            applyMask (mask);   // undo (it's an exclusive or)
        }
        applyMask (best);
        drawFormatBits (best);
    }

    void set (int x, int y, bool dark)
    {
        modules[(size_t) (y * size + x)] = dark;
        function[(size_t) (y * size + x)] = true;
    }

    void drawFunctionPatterns()
    {
        for (int i = 0; i < size; ++i)   // timing patterns
        {
            set (6, i, i % 2 == 0);
            set (i, 6, i % 2 == 0);
        }
        for (auto [cx, cy] : { std::pair<int, int> { 3, 3 }, { size - 4, 3 }, { 3, size - 4 } })   // finders, with their separators
            for (int dy = -4; dy <= 4; ++dy)
                for (int dx = -4; dx <= 4; ++dx)
                {
                    const int x = cx + dx, y = cy + dy, ring = juce::jmax (std::abs (dx), std::abs (dy));
                    if (juce::isPositiveAndBelow (x, size) && juce::isPositiveAndBelow (y, size))
                        set (x, y, ring != 2 && ring != 4);
                }
        const auto positions = alignmentPositions (version);
        const int n = (int) positions.size();
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
            {
                if ((i == 0 && j == 0) || (i == 0 && j == n - 1) || (i == n - 1 && j == 0))
                    continue;   // where the finders are
                for (int dy = -2; dy <= 2; ++dy)
                    for (int dx = -2; dx <= 2; ++dx)
                        set (positions[(size_t) i] + dx, positions[(size_t) j] + dy, juce::jmax (std::abs (dx), std::abs (dy)) != 1);
            }
        drawFormatBits (0);   // reserves its places (drawn again with the chosen mask)
        if (version >= 7)
        {
            int rem = version;
            for (int i = 0; i < 12; ++i)
                rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
            const int bits = version << 12 | rem;
            for (int i = 0; i < 18; ++i)
            {
                const bool dark = ((bits >> i) & 1) != 0;
                const int a = size - 11 + i % 3, b = i / 3;
                set (a, b, dark);
                set (b, a, dark);
            }
        }
    }

    // Error correction level M (00) and the mask, BCH-coded, in both copies, and the dark module.
    void drawFormatBits (int mask)
    {
        const int data = (0 << 3) | mask;
        int rem = data;
        for (int i = 0; i < 10; ++i)
            rem = (rem << 1) ^ ((rem >> 9) * 0x537);
        const int bits = ((data << 10) | rem) ^ 0x5412;
        auto bit = [bits] (int i) { return ((bits >> i) & 1) != 0; };
        for (int i = 0; i <= 5; ++i) set (8, i, bit (i));
        set (8, 7, bit (6));
        set (8, 8, bit (7));
        set (7, 8, bit (8));
        for (int i = 9; i < 15; ++i) set (14 - i, 8, bit (i));
        for (int i = 0; i < 8; ++i) set (size - 1 - i, 8, bit (i));
        for (int i = 8; i < 15; ++i) set (8, size - 15 + i, bit (i));
        set (8, size - 8, true);
    }

    // Up and down two columns at a time from the right, skipping the vertical timing pattern.
    void drawCodewords (const std::vector<juce::uint8>& stream)
    {
        size_t i = 0;
        for (int right = size - 1; right >= 1; right -= 2)
        {
            if (right == 6)
                right = 5;
            for (int vert = 0; vert < size; ++vert)
                for (int j = 0; j < 2; ++j)
                {
                    const int x = right - j;
                    const bool upward = ((right + 1) & 2) == 0;
                    const int y = upward ? size - 1 - vert : vert;
                    if (! function[(size_t) (y * size + x)] && i < stream.size() * 8)
                    {
                        modules[(size_t) (y * size + x)] = ((stream[i >> 3] >> (7 - (int) (i & 7))) & 1) != 0;
                        ++i;
                    }
                }
        }
    }

    void applyMask (int mask)
    {
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                bool flip = false;
                switch (mask)
                {
                    case 0: flip = (x + y) % 2 == 0; break;
                    case 1: flip = y % 2 == 0; break;
                    case 2: flip = x % 3 == 0; break;
                    case 3: flip = (x + y) % 3 == 0; break;
                    case 4: flip = (x / 3 + y / 2) % 2 == 0; break;
                    case 5: flip = x * y % 2 + x * y % 3 == 0; break;
                    case 6: flip = (x * y % 2 + x * y % 3) % 2 == 0; break;
                    default: flip = ((x + y) % 2 + x * y % 3) % 2 == 0; break;
                }
                if (flip && ! function[(size_t) (y * size + x)])
                    modules[(size_t) (y * size + x)] = ! modules[(size_t) (y * size + x)];
            }
    }

    // The standard's four penalties: long runs, 2x2 blocks, finder-like patterns, imbalance.
    long penalty() const
    {
        long score = 0;
        for (int pass = 0; pass < 2; ++pass)   // rows, then columns
            for (int a = 0; a < size; ++a)
            {
                int run = 0;
                bool last = false;
                for (int b = 0; b < size; ++b)
                {
                    const bool dark = pass == 0 ? isDark (b, a) : isDark (a, b);
                    if (b > 0 && dark == last) ++run; else { if (run >= 5) score += 3 + run - 5; run = 1; last = dark; }
                }
                if (run >= 5) score += 3 + run - 5;
                for (int b = 0; b + 10 < size; ++b)
                {
                    auto at = [&] (int k) { return pass == 0 ? isDark (b + k, a) : isDark (a, b + k); };
                    const bool core = at (0) && ! at (1) && at (2) && at (3) && at (4) && ! at (5) && at (6);
                    const bool coreLate = at (4) && ! at (5) && at (6) && at (7) && at (8) && ! at (9) && at (10);
                    if ((core && ! at (7) && ! at (8) && ! at (9) && ! at (10)) || (coreLate && ! at (0) && ! at (1) && ! at (2) && ! at (3)))
                        score += 40;
                }
            }
        int dark = 0;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                dark += isDark (x, y) ? 1 : 0;
                if (x + 1 < size && y + 1 < size && isDark (x, y) == isDark (x + 1, y) && isDark (x, y) == isDark (x, y + 1) && isDark (x, y) == isDark (x + 1, y + 1))
                    score += 3;
            }
        const int total = size * size;
        score += (std::abs (dark * 100 / total - 50) / 5) * 10;
        return score;
    }

    int version = 0, size = 0;
    std::vector<bool> modules, function;
};
