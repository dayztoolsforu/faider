#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../ProductName.h"
#include "Console.h"

// ABOUT FAIDER: the version, and what went into it: the libraries built in (with their
// authors and licences), the tools it works with, and how it was made. Opened from SETTINGS
// (ABOUT), FIND and the tray menu.
class AboutBox : public juce::Component
{
public:
    AboutBox() { setSize (700, neededHeight()); }

    // Tall enough for every line paint() draws (the margins, the header, each section and item).
    static int neededHeight()
    {
        int h = 18 * 2 + 34 + 18 + 14;
        for (auto& section : sections())
            h += 20 + 1 + 5 + 8 + (int) section.items.size() * (15 + 15 + 4);
        return h;
    }

    static void show()
    {
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (new AboutBox());
        options.dialogTitle = TRANS ("About %1").replace ("%1", Product::name);
        options.dialogBackgroundColour = Console::Colours::panelDark();
        options.useNativeTitleBar = true;
        options.resizable = false;
        options.launchAsync();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Console::Colours::panelDark());
        auto r = getLocalBounds().reduced (24, 18);

        g.setColour (Console::Colours::legend());
        g.setFont (Console::legendFont (26.0f));
        g.drawText (Product::nameUpper, r.removeFromTop (34), juce::Justification::left);
        g.setColour (Console::Colours::legendDim());
        g.setFont (Console::legendFont (Console::Type::body));
        g.drawFittedText (TRANS ("VERSION %1  -  AUDIO ROUTER, MIXER AND PLUGIN HOST FOR WINDOWS").replace ("%1", JUCE_APPLICATION_VERSION_STRING), r.removeFromTop (18), juce::Justification::left, 1, 0.7f);
        r.removeFromTop (14);

        for (auto& section : sections())
        {
            g.setColour (Console::Colours::legend());
            g.setFont (Console::legendFont (Console::Type::body));
            g.drawFittedText (juce::translate (section.title), r.removeFromTop (20), juce::Justification::left, 1, 0.7f);
            g.setColour (Console::Colours::rim());
            g.fillRect (r.removeFromTop (1));
            r.removeFromTop (5);
            for (auto& item : section.items)
            {
                g.setColour (Console::Colours::legend());
                g.setFont (Console::legendFont (Console::Type::legend));
                g.drawFittedText (juce::translate (item.name), r.removeFromTop (15), juce::Justification::left, 1, 0.7f);
                g.setColour (Console::Colours::legendDim());
                g.setFont (juce::FontOptions (Console::Type::body));
                g.drawFittedText (juce::translate (item.about), r.removeFromTop (15), juce::Justification::left, 1, 0.7f);
                r.removeFromTop (4);
            }
            r.removeFromTop (8);
        }
    }

private:
    struct Item { const char* name; const char* about; };
    struct Section { const char* title; std::vector<Item> items; };

    // (Translated where drawn: a static table is made once. The names of products and standards stay as they are.)
    static const std::vector<Section>& sections()
    {
        static const std::vector<Section> list {
            { NEEDS_TRANS ("BUILT ON"), {
                { "JUCE 8.0.12", NEEDS_TRANS ("RAW MATERIAL SOFTWARE - AUDIO DEVICES, PLUGIN HOSTING, FILE FORMATS, THE WHOLE INTERFACE (AGPLV3)") },
                { "RNNOISE 0.2", NEEDS_TRANS ("JEAN-MARC VALIN, XIPH.ORG FOUNDATION, MOZILLA, AMAZON - NEURAL NOISE REDUCTION (BSD 3-CLAUSE)") },
                { NEEDS_TRANS ("FLAC AND OGG VORBIS"), NEEDS_TRANS ("XIPH.ORG FOUNDATION, VIA JUCE - RECORDING FORMATS (BSD)") },
                { NEEDS_TRANS ("THEME ART"), NEEDS_TRANS ("KNOBS BY FAZIL IRAK AND PAULE AMCA (KNOBGALLERY), METAL TEXTURE FROM AMBIENTCG - PLUG-IN GREY (CC0)") },
                { "AUTOEQ", NEEDS_TRANS ("JAAKKO PASANEN - HEADPHONE CORRECTION CURVES (MIT)") },
            } },
            { NEEDS_TRANS ("STANDARDS"), {
                { "VST3", NEEDS_TRANS ("STEINBERG MEDIA TECHNOLOGIES - PLUGIN FORMAT. VST IS A TRADEMARK OF STEINBERG MEDIA TECHNOLOGIES GMBH") },
                { "ASIO", NEEDS_TRANS ("STEINBERG MEDIA TECHNOLOGIES - LOW-LATENCY DRIVERS. ASIO IS A TRADEMARK AND SOFTWARE OF STEINBERG") },
                { "WASAPI", NEEDS_TRANS ("MICROSOFT - WINDOWS AUDIO DEVICES AND PER-APP CAPTURE") },
                { "EBU R 128 / ITU-R BS.1770", NEEDS_TRANS ("LOUDNESS METERING (LUFS) AND TRUE PEAK") },
            } },
            { NEEDS_TRANS ("WORKS WITH (NOT INCLUDED)"), {
                { "VB-CABLE", NEEDS_TRANS ("VB-AUDIO SOFTWARE - VIRTUAL CABLES BETWEEN APPS") },
                { "FFMPEG", NEEDS_TRANS ("THE FFMPEG DEVELOPERS - MP3 RECORDING WHEN FFMPEG.EXE IS ON THE PATH (LGPL / GPL)") },
            } },
            { NEEDS_TRANS ("MADE WITH"), {
                { "C++20, CMAKE, MICROSOFT VISUAL C++", NEEDS_TRANS ("BUILT FROM SOURCE ON WINDOWS 11") },
            } },
        };
        return list;
    }
};
