// MilkyTracker as a plug-in. The tracker itself is tracker_host.cpp; this is the processor around it:
//  - it pulls the tracker's mix one beat packet at a time (192 samples at 48 kHz, the player's own tick grain),
//  - Free: the tracker plays as itself, MIDI notes play and record through it as from a MIDI keyboard,
//  - DAW Sync: the tracker's BPM is the host's times a ratio, and the song follows the host's transport,
//  - MIDI Trigger: a green zone of keys transposes, a blue zone plays each key's patterns one after another.
// The audio thread never takes the tracker's lock. MIDI for the tracker, sessions and the zones' pattern lists go
// through a worker thread that does.
#include <hollow/hollow.h>
#include "tracker_host.h"
#include "PlayerController.h"
#include "MasterMixer.h"
#include "ModuleEditor.h"
#include "XModule.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace hollow {

const Info& pluginInfo() {
    // its name in hosts' lists and title bars; the files are MilkyTrackerVST (CMakeLists.txt), the ids unchanged
    static const Info info{"studio.musica.milkytracker", "MilkyTracker.vst", "musica.studio", "https://github.com/musicastudio/MilkyTracker.vst",
                           "0.1.0", "A Fasttracker II compatible tracker", /*instrument*/ true, /*inputs*/ 0, /*outputs*/ 2,
                           /*midiIn*/ true, /*midiOut*/ false};
    return info;
}

namespace {

enum Mode { Free, Sync, Trigger };

// sync.ratio's choices: the tracker's BPM is the host's times this. FT2's ticks are 2.5 / BPM s long, so x1 at
// speed 6 is four rows a beat.
const double kRatios[] = {0.25, 0.5, 0.75, 1, 1.5, 2, 3, 4};

const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64(const std::string& in) {
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    for (size_t i = 0; i < in.size(); i += 3) {
        const size_t n = std::min<size_t>(3, in.size() - i);
        uint32_t v = (uint32_t)(uint8_t)in[i] << 16;
        if (n > 1) v |= (uint32_t)(uint8_t)in[i + 1] << 8;
        if (n > 2) v |= (uint8_t)in[i + 2];
        out += kB64[v >> 18 & 63];
        out += kB64[v >> 12 & 63];
        out += n > 1 ? kB64[v >> 6 & 63] : '=';
        out += n > 2 ? kB64[v & 63] : '=';
    }
    return out;
}

std::string unbase64(const std::string& in) {
    std::string out;
    uint32_t v = 0;
    int bits = 0;
    for (char c : in) {
        const char* p = std::strchr(kB64, c);
        if (!c || !p) continue;   // '=', line breaks
        v = v << 6 | (uint32_t)(p - kB64);
        if ((bits += 6) >= 8) {
            bits -= 8;
            out += (char)(v >> bits & 0xff);
        }
    }
    return out;
}

// A key's patterns as the text field has them: pattern numbers in hex, as the tracker shows them, up to 64.
std::vector<uint8_t> parsePatterns(const std::string& s) {
    std::vector<uint8_t> v;
    for (size_t i = 0; i < s.size() && v.size() < 64;) {
        if (!std::isxdigit((unsigned char)s[i])) { ++i; continue; }
        unsigned n = 0;
        for (; i < s.size() && std::isxdigit((unsigned char)s[i]); ++i) {
            const char c = (char)std::tolower((unsigned char)s[i]);
            n = std::min(n * 16 + (unsigned)(c <= '9' ? c - '0' : c - 'a' + 10), 255u);
        }
        v.push_back((uint8_t)n);
    }
    return v;
}

std::string showPatterns(const std::vector<uint8_t>& v) {
    std::string s;
    char b[4];
    for (uint8_t p : v) {
        std::snprintf(b, sizeof b, "%02X", p);
        s += (s.empty() ? "" : " ") + std::string(b);
    }
    return s;
}

// One key's patterns for the audio thread: the worker writes the length last, the audio thread reads it first.
struct Patterns {
    std::array<uint8_t, 64> list{};
    std::atomic<int> count{0};
};

// MIDI for the tracker, from the audio thread to the worker: single producer, single consumer.
struct NoteRing {
    struct Note { uint8_t note, velocity; };
    Note q[256];
    std::atomic<uint32_t> head{0}, tail{0};
    void push(uint8_t n, uint8_t v) {
        uint32_t h = head.load(std::memory_order_relaxed);
        if (h - tail.load(std::memory_order_acquire) >= 256) return;   // full: a mouse cannot get here
        q[h & 255] = {n, v};
        head.store(h + 1, std::memory_order_release);
    }
    bool pop(Note& n) {
        uint32_t t = tail.load(std::memory_order_relaxed);
        if (t == head.load(std::memory_order_acquire)) return false;
        n = q[t & 255];
        tail.store(t + 1, std::memory_order_release);
        return true;
    }
};

class MilkyProcessor : public Processor {
public:
    explicit MilkyProcessor(State& s) : state(s) {
        p.mode = s.indexOf("mode");
        p.volume = s.indexOf("master.volume");
        p.ratio = s.indexOf("sync.ratio");
        p.pitchFrom = s.indexOf("trigger.pitch_from");
        p.pitchCount = s.indexOf("trigger.pitch_count");
        p.home = s.indexOf("trigger.home");
        p.patFrom = s.indexOf("trigger.patterns_from");
        p.patCount = s.indexOf("trigger.patterns_count");
        p.latch = s.indexOf("trigger.latch");
        p.loop = s.indexOf("trigger.loop");
        p.key = s.indexOf("trigger.key");
        p.transpose = s.indexOf("trigger.transpose");
        p.pickSong = s.indexOf("gui.pick_song");
        p.pickKey = s.indexOf("gui.pick_key");
        p.scale = s.indexOf("gui.tracker_scale");
        events.reserve(512);
        host = std::make_shared<TrackerHost>(1024, 720, 48000, packetOf(48000));
        s.setPixels("tracker", host);
        worker = std::thread([this] { work(); });
    }

    ~MilkyProcessor() override {
        quit = true;
        if (worker.joinable()) worker.join();
        host->shutDown();
    }

    void prepare(double sampleRate, int) override {
        rate = sampleRate > 0 ? sampleRate : 48000;
        packet = packetOf((int)std::lround(rate));
        host->setHostAudio((int)std::lround(rate), packet);
        mix.assign((size_t)packet * 2, 0);
        fifo.assign((size_t)packet * 2, 0.0f);
        fifoPos = fifoLen = 0;
    }

    void midi(int offset, const uint8_t* b, int n) override {
        if (n < 3 || events.size() >= 512) return;
        const uint8_t type = b[0] & 0xf0;
        if (type == 0x90 || type == 0x80) events.push_back({offset, (uint8_t)(type == 0x90 && b[2] ? 0x90 : 0x80), b[1], b[2]});
    }

    void process(const float* const*, float* const* out, int frames) override {
        if (mix.empty()) {   // not prepared: the outputs arrive silent
            events.clear();
            return;
        }
        PlayerController* pc = controller();
        const int mode = (int)param(p.mode);
        if (pc) follow(pc, mode);
        shownTempo = transport.hasTempo ? transport.tempo : 0.0;
        shownPlaying = transport.playing;
        const double v = param(p.volume);
        const float gain = v <= -59.9 ? 0.0f : (float)(std::pow(10.0, v / 20) / 32768.0);
        size_t e = 0;
        for (int done = 0; done < frames;) {
            if (fifoPos >= fifoLen) {
                for (; e < events.size() && events[e].offset <= done; ++e) note(controller(), mode, events[e]);
                if (PlayerController* c = controller()) {
                    if (mode != Free) tickRate(c, mode);
                    c->hostTranspose(mode == Trigger ? (int)param(p.transpose) : 0);   // MIDI Trigger's pitch keys
                }
                MasterMixer* m = host->mixer;
                if (m && m->isPlaying()) m->mixerHandler(mix.data());
                else std::fill(mix.begin(), mix.end(), (int16_t)0);
                for (size_t i = 0; i < mix.size(); ++i) fifo[i] = mix[i] * gain;
                fifoPos = 0;
                fifoLen = packet;
            }
            const int n = std::min(frames - done, fifoLen - fifoPos);
            for (int i = 0; i < n; ++i) {
                out[0][done + i] = fifo[(size_t)(fifoPos + i) * 2];
                out[1][done + i] = fifo[(size_t)(fifoPos + i) * 2 + 1];
            }
            fifoPos += n;
            done += n;
        }
        for (; e < events.size(); ++e) note(controller(), mode, events[e]);
        events.clear();
        lastTransport = transport;
        lastFrames = frames;
    }

    // The host is about to save the session: the module goes into it as XM.
    void saving() override {
        std::string bytes, name;
        if (!host->save(bytes, name)) return;
        state.setData("module.name", name);
        state.setData("module", base64(bytes));
    }

private:
    struct Params { int mode, volume, ratio, pitchFrom, pitchCount, home, patFrom, patCount, latch, loop, key, transpose, pickSong, pickKey, scale; } p{};
    struct Event { int offset; uint8_t type, note, velocity; };

    State& state;
    std::shared_ptr<TrackerHost> host;
    std::thread worker;
    std::atomic<bool> quit{false};
    NoteRing toTracker;
    Patterns patterns[128];

    double rate = 48000;
    int packet = 192;
    std::vector<int16_t> mix;
    std::vector<float> fifo;
    int fifoPos = 0, fifoLen = 0;
    std::vector<Event> events;   // this block's notes; reserved, never grown on the audio thread
    Transport lastTransport;
    std::atomic<unsigned> restores{0};   // modules the worker has put into the tracker from a session
    unsigned restoresSeen = 0;           // audio thread: a new one stopped the song, so DAW Sync places it again
    int lastFrames = 0;
    int playingKey = -1;       // MIDI Trigger: the key whose patterns are playing
    PlayerController* used = nullptr;
    std::atomic<double> shownTempo{0};   // what the sync screen says (live.sync), from the audio thread
    std::atomic<bool> shownPlaying{false};

    static int packetOf(int sampleRate) { return std::max(1, 192 * sampleRate / 48000); }   // ChannelMixer's beat packet
    double param(int i) const { return i >= 0 ? state.get((size_t)i) : 0; }

    // The current tab's controller, while the mixer still has its device: a closing tab's controller is freed
    // only after the mixer drops its device, which happens on this thread, in mixerHandler.
    PlayerController* controller() {
        PlayerController* c = host->current.load();
        MasterMixer* m = host->mixer;
        return c && m && m->isDeviceAlive(c->hostDevice()) ? c : nullptr;
    }

    // The tracker's ticks from the host's tempo times the ratio; with no tempo from the host, its own BPM.
    void tickRate(PlayerController* c, int mode) {
        if (mode == Free || !transport.hasTempo) {
            c->hostTickRate(0);
            return;
        }
        c->hostTickRate(transport.tempo * ratio() * 2.0 / 5.0);   // FT2: BPM * 2 / 5 ticks a second
    }

    double ratio() const { return kRatios[std::clamp((int)param(p.ratio), 0, (int)(sizeof kRatios / sizeof kRatios[0]) - 1)]; }

    // DAW Sync: the song starts with the host's transport, where the host's position is, and goes there again
    // when the host jumps (a loop, a click on the timeline); stops with it. Between jumps the tempo keeps them
    // together, so a song's own Bxx and Dxx jumps play as written.
    void follow(PlayerController* c, int mode) {
        if (c != used) {
            if (used) lastTransport = Transport();
            used = c;
        }
        if (mode == Free) {
            c->hostTickRate(0);
            return;
        }
        tickRate(c, mode);
        if (mode != Sync) return;
        const Transport& t = transport;
        const unsigned r = restores;
        const bool reloaded = r != restoresSeen;   // a session loaded while the host plays
        restoresSeen = r;
        if (t.playing && t.hasBeats) {
            // a block without a position before this one (a host's first playing block can lack it) gave nothing to follow
            bool jumped = !lastTransport.playing || !lastTransport.hasBeats || reloaded;
            if (!jumped && lastTransport.hasBeats && lastTransport.hasTempo) {
                const double expected = lastTransport.beats + lastFrames / rate * lastTransport.tempo / 60.0;
                jumped = std::fabs(t.beats - expected) > 0.05;
            }
            if (jumped) locate(c, t.beats);
        } else if (lastTransport.playing && !t.playing) {
            c->hostStop();
        }
    }

    // The order and row the host's position falls on: its ticks at the ratio, at the song's starting speed, laid
    // along the order list from the top, and round again past the end (the song loops).
    void locate(PlayerController* c, double beats) {
        ModuleEditor* ed = c->getModuleEditor();
        XModule* mod = ed ? ed->getModule() : nullptr;
        if (!mod || !mod->header.ordnum) return;
        const int speed = std::max<int>(mod->header.tempo, 1);
        double rows = std::max(0.0, beats * 24.0 * ratio() / speed);   // 24 ticks a beat at x1 (FT2's 2.5 / BPM s)
        long long total = 0;
        for (int o = 0; o < mod->header.ordnum; ++o) total += mod->phead[mod->header.ord[o]].rows;
        if (total <= 0) return;
        long long row = (long long)std::floor(rows) % total;
        for (int o = 0; o < mod->header.ordnum; ++o) {
            const int n = mod->phead[mod->header.ord[o]].rows;
            if (row < n) {
                c->hostPlaySong(o, (int)row);
                return;
            }
            row -= n;
        }
    }

    // A note from the host or the on-screen keys. MIDI Trigger takes the notes in its zones; every other note goes
    // to the tracker, as its own MIDI input would.
    void note(PlayerController* c, int mode, const Event& e) {
        const bool on = e.type == 0x90;
        if (mode == Trigger) {
            const int pf = (int)param(p.pitchFrom), pn = (int)param(p.pitchCount);
            const int qf = (int)param(p.patFrom), qn = (int)param(p.patCount);
            if (e.note >= pf && e.note < pf + pn) {   // green: the pitch, held until another key moves it
                if (on && p.transpose >= 0) state.set((size_t)p.transpose, e.note - (int)param(p.home));
                return;
            }
            if (e.note >= qf && e.note < qf + qn) {   // blue: this key's patterns
                trigger(c, e.note, on);
                return;
            }
        }
        toTracker.push(e.note, on ? std::max<uint8_t>(e.velocity, 1) : 0);
    }

    // Gate plays a key's patterns while it is held; Latch starts them on one press and stops them on the next,
    // and another key takes over at once either way.
    void trigger(PlayerController* c, int key, bool on) {
        const bool latch = param(p.latch) > 0.5;
        if (on) {
            if (p.key >= 0) state.set((size_t)p.key, key);   // the key the pattern list edits
            if (!c) return;
            if (latch && key == playingKey && c->hostIsPlaying()) {
                c->hostStop();
                playingKey = -1;
                return;
            }
            Patterns& q = patterns[key];
            const int n = q.count.load(std::memory_order_acquire);
            if (n <= 0) return;
            if (c->hostPlayPatterns(q.list.data(), n, param(p.loop) > 0.5)) playingKey = key;
        } else if (!latch && key == playingKey && c) {
            c->hostStop();
            playingKey = -1;
        }
    }

    // ---- the worker -----------------------------------------------------------------------------------

    void work() {
        using Clock = std::chrono::steady_clock;
        using ms = std::chrono::milliseconds;
        unsigned loads = ~0u;
        int scaleShown = -1, scaleSet = -1;   // the tracker scale as last read from the tracker's settings and from the param
        std::string seen[128];
        Clock::time_point lists, screen, song;   // when each was last brought up to date
        while (!quit) {
            for (NoteRing::Note m; toTracker.pop(m);) host->note(m.note, m.velocity);
            // a session the host loaded: its module replaces the tracker's
            const unsigned l = state.loads();
            if (!(l & 1) && l != loads) {
                loads = l;
                if (state.hasData("module")) host->restore(unbase64(state.data("module")), state.data("module.name")), ++restores;
                song = Clock::time_point();
                scaleShown = scaleSet = -1;   // the tracker's own setting, not the session's, says how big it is
            }
            // by the clock, since a short sleep lasts a whole timer tick (15.6 ms) on Windows unless the host shortened it
            const Clock::time_point now = Clock::now();
            if (now - lists >= ms(10)) {
                syncPatterns(seen);
                syncScale(scaleShown, scaleSet, loads);
                lists = now;
            }
            if (now - screen >= ms(100)) showSync(), screen = now;
            if (now - song >= ms(250)) state.setData("live.patterns.song", host->patternRows()), song = now;
            std::this_thread::sleep_for(ms(2));
        }
    }

    // MilkyTracker's own Scale (its Config page) and the column's Tracker 1x and 2x are one setting, kept in the
    // tracker's settings, so it is the user's and not the session's: whichever moved last wins, and at the start, or
    // after a session loads, the tracker's. A load (its count odd while it runs) may set the param from the session
    // while this waits for the tracker's lock, so nothing is done unless no load began meanwhile or is still to be
    // handled (handled: the last count the load branch of work() took up).
    void syncScale(int& shown, int& set, unsigned handled) {
        if (p.scale < 0) return;
        const unsigned before = state.loads();
        const int tracker = host->scale(), param = (int)std::lround(state.get((size_t)p.scale));
        if (before != handled || state.loads() != before) return;
        if (shown < 0 || tracker != shown) {
            state.set((size_t)p.scale, tracker);
            shown = set = tracker;
        } else if (param != set) {
            host->setScale(param);
            shown = set = param;
        }
    }

    // The sync screen: the host's tempo and transport, and the BPM the tracker plays at.
    void showSync() {
        const double t = shownTempo;
        char b[128];
        if (t <= 0) std::snprintf(b, sizeof b, "No tempo from the host\nTracker: its own BPM");
        else if ((int)param(p.mode) == Free) std::snprintf(b, sizeof b, "Host %.1f BPM, %s\nTracker: its own BPM", t, shownPlaying ? "playing" : "stopped");
        else std::snprintf(b, sizeof b, "Host %.1f BPM, %s\nTracker %.1f BPM", t, shownPlaying ? "playing" : "stopped", t * ratio());
        state.setData("live.sync", b);
    }

    // Each pattern key's list is text data trigger.seq.<key>, saved with the session. The patterns dialog edits the
    // picked key's (trigger.key): its buttons leave a command in live.patterns.cmd, done here and cleared, on the
    // rows the dialog's two lists have picked (gui.pick_song, a pattern of the song; gui.pick_key, a place in the
    // list). What the dialog and the column show is live.* data, never saved.
    void syncPatterns(std::string* seen) {
        const int k = p.key >= 0 ? std::clamp((int)std::lround(state.get((size_t)p.key)), 0, 127) : -1;
        if (k >= 0) {
            const std::string seq = "trigger.seq." + std::to_string(k);
            std::vector<uint8_t> v = parsePatterns(state.data(seq));
            const std::string cmd = state.data("live.patterns.cmd");
            if (!cmd.empty()) {
                state.setData("live.patterns.cmd", "");
                int at = std::clamp((int)param(p.pickKey), 0, std::max((int)v.size() - 1, 0));
                if (cmd == "add" && v.size() < 64) {
                    v.push_back((uint8_t)std::clamp((int)param(p.pickSong), 0, 255));
                    at = (int)v.size() - 1;
                } else if (cmd == "remove" && at < (int)v.size()) {
                    v.erase(v.begin() + at);
                    at = std::min(at, std::max((int)v.size() - 1, 0));
                } else if (cmd == "up" && at > 0 && at < (int)v.size()) {
                    std::swap(v[(size_t)at], v[(size_t)at - 1]);
                    --at;
                } else if (cmd == "down" && at + 1 < (int)v.size()) {
                    std::swap(v[(size_t)at], v[(size_t)at + 1]);
                    ++at;
                } else if (cmd == "clear") {
                    v.clear();
                    at = 0;
                }
                state.setData(seq, showPatterns(v));
                if (p.pickKey >= 0) state.set((size_t)p.pickKey, at);
            }
            static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
            state.setData("live.trigger.keyname", names[k % 12] + std::to_string(k / 12 - 1));
            state.setData("live.trigger.edit", v.empty() ? std::string("none") : showPatterns(v));
            std::string rows;
            for (size_t i = 0; i < v.size(); ++i) {
                char b[16];
                std::snprintf(b, sizeof b, "%d\t%02X\n", (int)i + 1, v[i]);
                rows += b;
            }
            state.setData("live.patterns.key", rows);
        }
        for (int n = 0; n < 128; ++n) {
            const std::string s = state.data("trigger.seq." + std::to_string(n));
            if (s == seen[n]) continue;
            seen[n] = s;
            const std::vector<uint8_t> v = parsePatterns(s);
            patterns[n].count.store(0, std::memory_order_release);
            std::copy(v.begin(), v.end(), patterns[n].list.begin());
            patterns[n].count.store((int)v.size(), std::memory_order_release);
        }
    }
};

} // namespace

std::unique_ptr<Processor> createProcessor(State& state) { return std::make_unique<MilkyProcessor>(state); }

} // namespace hollow
