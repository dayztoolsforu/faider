#include "HeadphoneEq.h"
#include "PatchbayAssets.h"

namespace HeadphoneEq
{
    // assets/autoeq/profiles.txt: "name|measured by|preamp dB|type,Hz,gain dB,Q;..." one per line.
    const std::vector<Profile>& profiles()
    {
        static const std::vector<Profile> list = []
        {
            std::vector<Profile> all;
            const juce::String text (juce::CharPointer_UTF8 (PatchbayAssets::profiles_txt), (size_t) PatchbayAssets::profiles_txtSize);
            for (auto& line : juce::StringArray::fromLines (text))
            {
                if (line.startsWith ("#") || line.trim().isEmpty())
                    continue;
                const auto parts = juce::StringArray::fromTokens (line, "|", {});
                if (parts.size() < 4)
                    continue;
                Profile p { parts[0], parts[1], parts[2].getFloatValue(), {} };
                for (auto& f : juce::StringArray::fromTokens (parts[3], ";", {}))
                {
                    const auto v = juce::StringArray::fromTokens (f, ",", {});
                    if (v.size() == 4)
                        p.filters.push_back ({ v[0] == "LSC" ? 'L' : v[0] == "HSC" ? 'H' : 'P', v[1].getFloatValue(), v[2].getFloatValue(), v[3].getFloatValue() });
                }
                all.push_back (std::move (p));
            }
            std::sort (all.begin(), all.end(), [] (const Profile& a, const Profile& b) { return a.name.compareIgnoreCase (b.name) < 0; });
            return all;
        }();
        return list;
    }

    const Profile* find (const juce::String& name)
    {
        for (auto& p : profiles())
            if (p.name == name)
                return &p;
        return nullptr;
    }
}
