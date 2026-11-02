// Makes QR codes (src/ui/QrCode.h) for a few links and prints each as rows of 0 and 1, with its
// version, so a decoder can read them back (the codes were checked by decoding these rows).
// Also checks the sizes: a version's size is 4v + 17, and too long a text gives none.
#include "../src/ui/QrCode.h"
#include <iostream>

int main (int argc, char** argv)
{
    int failures = 0;
    auto check = [&failures] (bool ok, const juce::String& what) { std::cout << (ok ? "PASS  " : "FAIL  ") << what << "\n"; failures += ok ? 0 : 1; };

    const juce::StringArray texts { "http://192.168.8.185:9123/?key=GSSVBCZN", "HELLO",
                                    "http://192.168.100.200:9123/?key=ABCDEFGH&extra=" + juce::String::repeatedString ("x", 60),
                                    "https://example.com/" + juce::String::repeatedString ("a", 150) };
    const bool rows = argc > 1 && juce::String (argv[1]) == "--rows";
    for (auto& t : texts)
    {
        const auto qr = QrCode::encode (t);
        check (qr.getSize() == qr.getVersion() * 4 + 17 && qr.getVersion() >= 1, "\"" + t.substring (0, 40) + "\" (" + juce::String (t.length()) + " bytes): version "
                                                                                     + juce::String (qr.getVersion()) + ", " + juce::String (qr.getSize()) + " modules");
        if (rows)
        {
            std::cout << "ROWS " << t << "\n";
            for (int y = 0; y < qr.getSize(); ++y)
            {
                for (int x = 0; x < qr.getSize(); ++x)
                    std::cout << (qr.isDark (x, y) ? '1' : '0');
                std::cout << "\n";
            }
            std::cout << "END\n";
        }
    }
    check (QrCode::encode (juce::String::repeatedString ("z", 300)).getSize() == 0, "300 bytes: too long, no code");

    std::cout << (failures == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string (failures)) << "\n";
    return failures == 0 ? 0 : 1;
}
