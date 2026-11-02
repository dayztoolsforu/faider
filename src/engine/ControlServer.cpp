#include "ControlServer.h"
#include "AudioEngine.h"
#include "PhonePage.h"
#include "../ui/Theme.h"
#include <limits>
#include <random>

ControlServer::ControlServer (AudioEngine& e) : juce::Thread ("Patchbay control API"), engine (e) {}

ControlServer::~ControlServer()
{
    stop();            // the thread first: while it runs it reads `alive` (onMessageThread)
    alive = nullptr;   // a command already handed to the message thread won't run now
}

bool ControlServer::setPort (int newPort)
{
    stop();
    return newPort <= 0 || open (newPort);
}

void ControlServer::stop()
{
    signalThreadShouldExit();
    if (listener != nullptr)
        listener->close();
    stopThread (5000);   // (a request in progress stops within a fifth of a second)
    listener = nullptr;
    port = 0;
}

bool ControlServer::open (int newPort)
{
    auto socket = std::make_unique<juce::StreamingSocket>();
    if (! socket->createListener (newPort, phone ? juce::String() : juce::String ("127.0.0.1")))   // phone control: every network
        return false;
    listener = std::move (socket);
    port = newPort;
    startThread();
    return true;
}

// The server is stopped while phone control changes, so its thread never reads the key mid-change.
// With the API off there's nothing to open to the network: phone control stays off.
bool ControlServer::setPhone (bool on, const juce::String& key)
{
    const int was = port;
    if (was <= 0)
    {
        phone = false;   // (nothing running to race with)
        return ! on;
    }
    stop();
    phone = on;
    phoneKey = key;
    if (open (was))
        return true;
    phone = false;   // couldn't open it for the network: back to this PC only
    open (was);      // (if even that fails, the server is off: getPort() is 0, for the caller to show)
    return false;
}

juce::StringArray ControlServer::phoneAddresses (int port, const juce::String& key)
{
    juce::StringArray urls;
    for (auto& a : juce::IPAddress::getAllAddresses (false))
    {
        const int first = a.address[0];
        if (a != juce::IPAddress::local() && ! a.toString().startsWith ("169.254.")   // not this PC's own, nor a link with no network,
            && ! (first >= 224 && first <= 239))                                     // nor a multicast group
            urls.add ("http://" + a.toString() + ":" + juce::String (port) + "/?key=" + key);
    }
    return urls;
}

juce::String ControlServer::newKey()
{
    // 12 of 32 letters and digits (60 bits), from the system's secure random numbers (MSVC's
    // random_device), not a generator seeded from the clock.
    const juce::String chars ("ABCDEFGHJKLMNPQRSTUVWXYZ23456789");   // no 0/O or 1/I to misread
    std::random_device random;
    std::uniform_int_distribution<int> pick (0, chars.length() - 1);
    juce::String key;
    for (int i = 0; i < 12; ++i)
        key += chars[pick (random)];
    return key;
}

bool ControlServer::isOldKey (const juce::String& key)
{
    return key.length() < 12;
}

void ControlServer::run()
{
    while (! threadShouldExit())
    {
        if (listener->waitUntilReady (true, 200) != 1)
            continue;
        std::unique_ptr<juce::StreamingSocket> client (listener->waitForNextConnection());
        if (client != nullptr)
            serve (*client);
    }
}

bool ControlServer::onMessageThread (std::function<void()> f)
{
    auto done = std::make_shared<juce::WaitableEvent>();
    juce::MessageManager::callAsync ([f = std::move (f), done, guard = std::weak_ptr<bool> (alive)]
    {
        if (guard.lock() != nullptr)
            f();
        done->signal();
    });
    while (! done->wait (100))
        if (threadShouldExit())
            return false;
    return true;
}

void ControlServer::serve (juce::StreamingSocket& client)
{
    const auto from = client.getHostName();
    const bool thisPC = from == "127.0.0.1" || from == "::1" || from == "localhost";   // (no address: not trusted)

    // The request line and headers (a command carries nothing else that's needed).
    juce::MemoryOutputStream request;
    char buffer[2048];
    const auto started = juce::Time::getMillisecondCounter();
    // A client that connects and sends nothing (a browser's speculative connection, a stalled
    // phone) mustn't hold up everyone else, as this thread serves one request at a time. This PC:
    // 2 s for the whole request. Another device: its first bytes within 0.3 s and the whole request
    // within 1 s (a phone on Wi-Fi sends its few hundred bytes at once).
    const juce::uint32 firstBytesWithin = thisPC ? 2000 : 300, allWithin = thisPC ? 2000 : 1000;
    while (request.getDataSize() < 16384 && ! request.toString().contains ("\r\n\r\n"))
    {
        if (threadShouldExit())
            return;   // the server is stopping (Patchbay closing, the port changed)
        if (juce::Time::getMillisecondCounter() - started > (request.getDataSize() == 0 ? firstBytesWithin : allWithin))
            return;
        const int ready = client.waitUntilReady (true, 100);
        if (ready < 0)
            return;
        if (ready == 0)
            continue;   // nothing yet: read only once there is
        const int got = client.read (buffer, (int) sizeof (buffer), false);
        if (got <= 0)
            break;
        request.write (buffer, (size_t) got);
    }

    juce::StringArray lines;
    lines.addLines (request.toString().upToFirstOccurrenceOf ("\r\n\r\n", false, false));
    const auto method = lines[0].upToFirstOccurrenceOf (" ", false, false);
    const auto target = lines[0].fromFirstOccurrenceOf (" ", false, false).upToLastOccurrenceOf (" ", false, false);
    const bool marked = std::any_of (lines.begin(), lines.end(), [] (const juce::String& l)
                                     { return l.upToFirstOccurrenceOf (":", false, false).trim().equalsIgnoreCase ("X-Patchbay"); });

    auto respond = [&client] (int code, const juce::String& status, const juce::String& body, const char* type = "application/json")
    {
        const auto bytes = body.toUTF8();
        const auto head = "HTTP/1.1 " + juce::String (code) + " " + status + "\r\nContent-Type: " + type + "; charset=utf-8\r\nContent-Length: "
                          + juce::String ((int) std::strlen (bytes)) + "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
        client.write (head.toRawUTF8(), (int) head.getNumBytesAsUTF8());
        client.write (bytes, (int) std::strlen (bytes));
    };

    juce::String value, key;
    juce::StringArray query;
    query.addTokens (target.fromFirstOccurrenceOf ("?", false, false), "&", "");
    for (auto& q : query)
    {
        const auto name = q.upToFirstOccurrenceOf ("=", false, false), v = juce::URL::removeEscapeChars (q.fromFirstOccurrenceOf ("=", false, false));
        if (name == "value") value = v;
        if (name == "key")   key = v;
    }
    for (auto& l : lines)
        if (l.upToFirstOccurrenceOf (":", false, false).trim().equalsIgnoreCase ("X-Patchbay-Key"))
            key = l.fromFirstOccurrenceOf (":", false, false).trim();

    juce::StringArray parts;
    parts.addTokens (target.upToFirstOccurrenceOf ("?", false, false), "/", "");
    parts.removeEmptyStrings();
    for (auto& p : parts)
        p = juce::URL::removeEscapeChars (p);
    // Only / and /api/... exist. Anything else (the browser's own /favicon.ico, a touch icon) is
    // turned away before the key is looked at, so it never counts as a wrong key.
    if (! parts.isEmpty() && parts[0] != "api")
        return respond (404, "Not Found", "{\"error\":\"not found\"}");

    // Trusted without a key: a request from this PC that also names this PC as its Host (a deck
    // plugin calling 127.0.0.1 or localhost). A web page whose own name has been pointed at this
    // PC (DNS rebinding) still sends its own name as the Host, so it isn't trusted. Anything else
    // (a phone on the network) needs phone control on and its key.
    juce::String host;
    for (auto& l : lines)
        if (l.upToFirstOccurrenceOf (":", false, false).trim().equalsIgnoreCase ("Host"))
            host = l.fromFirstOccurrenceOf (":", false, false).trim().toLowerCase();
    const auto hostName = host.startsWith ("[") ? host.upToFirstOccurrenceOf ("]", true, false) : host.upToFirstOccurrenceOf (":", false, false);
    const bool namesThisPC = hostName == "127.0.0.1" || hostName == "localhost" || hostName == "[::1]";
    // A device that keeps getting the key wrong waits: five wrong in a minute and it's turned away
    // for that minute (so the key can't be guessed at network speed). Only a key that's there and
    // wrong counts (not a request with none); the right key clears the device's count.
    if (! (thisPC && namesThisPC))
    {
        const auto now = juce::Time::getMillisecondCounter();
        auto tries = wrongKeys.find (from);
        if (tries != wrongKeys.end() && now - tries->second.since > 60000)
        {
            wrongKeys.erase (tries);   // its minute is over
            tries = wrongKeys.end();
        }
        if (tries != wrongKeys.end() && tries->second.count >= 5)
            return respond (429, "Too Many Requests", "{\"error\":\"too many wrong keys: wait a minute\"}");
        if (! (phone && phoneKey.isNotEmpty() && key == phoneKey))
        {
            if (key.isNotEmpty())
            {
                if (tries == wrongKeys.end())
                {
                    if (wrongKeys.size() >= 256)   // (many devices: forget those whose minute is over)
                        for (auto i = wrongKeys.begin(); i != wrongKeys.end();)
                            i = now - i->second.since > 60000 ? wrongKeys.erase (i) : std::next (i);
                    tries = wrongKeys.emplace (from, Tries { now, 0 }).first;
                }
                ++tries->second.count;
            }
            return respond (403, "Forbidden", "{\"error\":\"phone control is off, or the key is wrong\"}");
        }
        if (tries != wrongKeys.end())
            wrongKeys.erase (tries);
    }

    if (method == "GET" && parts.isEmpty())
        return respond (200, "OK", PhonePage::html, "text/html");
    if (parts[0] != "api")
        return respond (404, "Not Found", "{\"error\":\"not found\"}");
    parts.remove (0);

    if (method == "GET" && parts.size() == 1 && (parts[0] == "state" || parts[0] == "meters"))
    {
        auto json = std::make_shared<juce::String>();
        if (onMessageThread ([this, json, meters = parts[0] == "meters"] { *json = meters ? this->meters() : state(); }))
            respond (200, "OK", *json);
        return;
    }
    if (method != "POST")
        return respond (405, "Method Not Allowed", "{\"error\":\"commands are POST\"}");
    if (! marked)
        return respond (403, "Forbidden", "{\"error\":\"commands need the header X-Patchbay: 1\"}");

    // A value that isn't a plain number ("nan", "inf", "loud") goes in as NaN, which perform() refuses.
    const float number = value.isEmpty() || value.containsOnly ("0123456789.-+eE") ? value.getFloatValue()
                                                                                    : std::numeric_limits<float>::quiet_NaN();
    auto ok = std::make_shared<bool> (false);
    if (onMessageThread ([this, parts, has = value.isNotEmpty(), number, ok] { *ok = engine.getRemote().perform (parts, has, number); }))
        respond (*ok ? 200 : 404, *ok ? "OK" : "Not Found", *ok ? "{\"ok\":true}" : "{\"error\":\"no such strip, scene or macro, or a bad value\"}");
}

// A gain or peak in dB for the JSON, -100 for silence (and for inf/nan, which aren't JSON).
static double db (float gain)
{
    return ! std::isfinite (gain) || gain <= 0.00001f ? -100.0 : juce::jmax (-100.0, (double) juce::Decibels::gainToDecibels (gain));
}

// Every channel's peaks, left and right in dB (-100: silence), as { "input:MIC FOCUS": [-18.2, -17.9], ... }.
// Small and quick, for meters polled 20 or more times a second.
juce::String ControlServer::meters() const
{
    juce::String out ("{");
    auto add = [&] (const juce::String& key, float l, float r)
    {
        out << (out.length() > 1 ? "," : "") << juce::JSON::toString (key) << ":[" << juce::String (db (l), 1) << "," << juce::String (db (r), 1) << "]";
    };
    for (auto& s : engine.getStrips()) add ("input:" + s->name, s->peakL.load(), s->peakR.load());
    for (auto& p : engine.getBuses())  add ("bus:" + p->name, p->peakL.load(), p->peakR.load());
    for (auto& b : engine.getMasters())  add ("output:" + b->name, b->peakL.load(), b->peakR.load());
    return out + "}";
}

juce::String ControlServer::state() const
{
    auto strip = [&] (const AudioEngine::Strip& s, bool input)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", s.name);
        o->setProperty ("fader", db (s.gain.load()));
        o->setProperty ("mute", s.mute.load());
        if (input)
            o->setProperty ("solo", s.solo.load());
        o->setProperty ("pan", s.pan.load());
        o->setProperty ("level", db (juce::jmax (s.peakL.load(), s.peakR.load())));
        o->setProperty ("on", s.enabled.load());
        return juce::var (o);
    };

    juce::Array<juce::var> inputs, masters, outputs, scenes, macros;
    for (auto& s : engine.getStrips()) inputs.add (strip (*s, true));
    for (auto& p : engine.getBuses())  masters.add (strip (*p, false));
    for (auto& b : engine.getMasters())
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", b->name);
        o->setProperty ("fader", db (b->gain.load()));
        o->setProperty ("mute", b->mute.load());
        o->setProperty ("level", db (juce::jmax (b->peakL.load(), b->peakR.load())));
        o->setProperty ("on", b->enabled.load());
        outputs.add (juce::var (o));
    }
    const auto& sc = engine.getScenes();
    for (auto& s : sc.list) scenes.add (s.name);
    for (auto& m : engine.getMacros().list) macros.add (m.name);
    juce::Array<juce::var> pads;
    for (int i = 0; i < Pads::count; ++i)
        if (const auto& p = engine.getPads().pad (i); p.sound != nullptr || p.macro.isNotEmpty())
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("pad", i + 1);
            o->setProperty ("name", p.name.isNotEmpty() ? p.name : p.macro);
            pads.add (juce::var (o));
        }

    auto* root = new juce::DynamicObject();
    root->setProperty ("inputs", inputs);
    root->setProperty ("buses", masters);
    root->setProperty ("outputs", outputs);
    root->setProperty ("scenes", scenes);
    root->setProperty ("activeScene", juce::isPositiveAndBelow (sc.active, (int) sc.list.size()) ? sc.list[(size_t) sc.active].name : juce::String());
    root->setProperty ("macros", macros);
    root->setProperty ("pads", pads);

    // The desk's colours, so the phone page wears them too.
    const auto& t = Theme::get();
    auto* theme = new juce::DynamicObject();
    auto colour = [] (juce::Colour c) { return "#" + c.toDisplayString (false); };
    theme->setProperty ("background", colour (t.background));
    theme->setProperty ("panel", colour (t.panel));
    theme->setProperty ("panelDark", colour (t.panelDark));
    theme->setProperty ("rim", colour (t.rim));
    theme->setProperty ("legend", colour (t.legend));
    theme->setProperty ("legendDim", colour (t.legendDim));
    theme->setProperty ("key", colour (t.capTop));
    theme->setProperty ("keyText", colour (t.capText));
    theme->setProperty ("lit", colour (t.ledGreen));
    theme->setProperty ("mute", colour (t.muteLamp));
    theme->setProperty ("solo", colour (t.soloLamp));
    theme->setProperty ("glass", colour (t.screenGlass));
    theme->setProperty ("screen", colour (t.screenLit));
    theme->setProperty ("radius", (double) juce::jmin (22.0f, t.keyRadius));
    theme->setProperty ("font", t.font);
    root->setProperty ("theme", juce::var (theme));
    return juce::JSON::toString (juce::var (root), true);
}
