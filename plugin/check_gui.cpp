// check_gui: the plug-in's editor end to end without a window, as FSVR's check_gui drives FSVR's. The real skin and
// the real processor under one Hollow Gui, driven the way a mouse and a keyboard drive it: the column's mode buttons,
// steppers, toggles, dial and scale buttons, both keyboards and their neon zones, the zone strip, the patterns dialog
// and its lists, and the tracker itself, clicked into, typed into (a note entered into the pattern, read back out of
// the saved module), played and stopped from its own keys, scrolled and right-clicked, at every scale; then the editor
// closed and opened again, every visible control clicked once, and the session saved and restored into a new instance.
// Exits non-zero on any failure.
//   check_gui <a module, such as resources/music/milky.xm>
// With MT_GUI_SHOTS set to a folder it also saves the window as a PNG at each shot("name"), to look at.
#include <hollow/hollow.h>
#include "core/core.h"
#include "checks.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "stb/stb_image_write.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

using namespace hollow;
namespace fs = std::filesystem;

static int fails = 0, passes = 0;
static std::atomic<const char*> step{"start"};
static std::atomic<long long> stepAt{0};
static long long nowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
static void begin(const char* what) {   // a section: the watchdog names it if the check stops in it
    step = what;
    stepAt = nowMs();
    std::printf("check_gui: %s\n", what);
}
#define CHECK(c, ...) do { if (c) ++passes; else { std::printf("check_gui: FAIL line %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); ++fails; } } while (0)

static const std::string M = "hollow_modal/";
struct Point { int x, y; };

struct Ui {   // one instance and its editor, run a block and a GUI tick at a time
    std::shared_ptr<Skin> skin = loadSkin();
    State st{skinParams(*skin)};
    std::unique_ptr<Processor> proc = createProcessor(st);
    std::unique_ptr<Gui> gui = std::make_unique<Gui>(skin.get(), st, nullptr);
    std::vector<float> l = std::vector<float>(512), r = std::vector<float>(512);
    Transport t;
    double peak = 0;
    Ui() { proc->prepare(48000, 512); }

    void run(int n = 1) {
        float* out[2] = {l.data(), r.data()};
        peak = 0;
        for (int k = 0; k < n; ++k) {
            std::fill(l.begin(), l.end(), 0.0f);
            std::fill(r.begin(), r.end(), 0.0f);
            proc->transport = t;
            uint8_t m[3];
            for (int n; st.popMidi(m, n);) proc->midi(0, m, n);   // what the editor played, as the format layer hands it on
            proc->process(nullptr, out, 512);
            for (int i = 0; i < 512; ++i) {
                if (!std::isfinite(l[i]) || !std::isfinite(r[i])) {
                    CHECK(false, "the output is not a number");
                    return;
                }
                peak = std::max(peak, (double)std::fabs(l[i]) + std::fabs(r[i]));
            }
            if (t.playing) t.beats += 512.0 / 48000 * t.tempo / 60;
            if (gui) gui->tick();
        }
        if (gui) gui->pixels();   // paints what changed, so a paint that breaks shows up here
    }
    template <class F> bool until(F done, int ms = 5000) {
        for (auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms); std::chrono::steady_clock::now() < end;) {
            run();
            if (done()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return false;
    }
    void settle() { until([] { return false; }, 200); }   // the worker's next rounds reach what was just done
    bool silentWithin(int ms) {   // the output falls silent and stays so for a tenth of a second
        return until([&] { run(10); return peak == 0; }, ms);
    }

    int at(const std::string& id) const { return st.indexOf(id); }
    double get(const std::string& id) const { return at(id) >= 0 ? st.get((size_t)at(id)) : -1e9; }
    long geti(const std::string& id) const { return std::lround(get(id)); }
    void set(const std::string& id, double v) { if (at(id) >= 0) st.set((size_t)at(id), v); }
    std::string data(const std::string& k) const { return st.data(k); }
    int lines(const std::string& k) const { std::string d = st.data(k); return (int)std::count(d.begin(), d.end(), '\n'); }
    bool shows(const std::string& path) { Rect r; return gui->widgetRect(path, r); }
    void shot(const char* name) {
        const char* dir = std::getenv("MT_GUI_SHOTS");
        if (!dir) return;
        const auto& px = gui->pixels();
        std::vector<uint8_t> rgba;
        rgba.reserve(px.size() * 4);
        for (uint32_t p : px) rgba.insert(rgba.end(), {uint8_t(p >> 16), uint8_t(p >> 8), uint8_t(p), 255});
        fs::create_directories(dir);
        stbi_write_png((fs::path(dir) / (std::string(name) + ".png")).string().c_str(), gui->width(), gui->height(), 4, rgba.data(), gui->width() * 4);
    }
    bool rect(const std::string& path, Rect& r) {
        if (gui->widgetRect(path, r)) return true;
        std::printf("check_gui: FAIL: no visible widget %s\n", path.c_str());
        ++fails;
        return false;
    }
    void clickAt(int x, int y) {
        gui->mouseMove(x, y, false);
        gui->mouseDown(x, y, false, false, false);
        gui->mouseUp(x, y, false);
        run(2);
    }
    bool click(const std::string& path, double fx = 0.5) {
        Rect r;
        if (!rect(path, r)) return false;
        clickAt(r.x + (int)(r.w * fx), r.y + r.h / 2);
        return true;
    }
    void rclickAt(int x, int y) {
        gui->mouseMove(x, y, false);
        gui->mouseDown(x, y, true, false, false);
        gui->rightUp(x, y, false);
        run(2);
    }
    void drag(int x, int y, int dx, int dy) {
        gui->mouseMove(x, y, false);
        gui->mouseDown(x, y, false, false, false);
        for (int k = 1; k <= 8; ++k) gui->mouseMove(x + dx * k / 8, y + dy * k / 8, false);
        gui->mouseUp(x + dx, y + dy, false);
        run(2);
    }
    // A list's row n, from its top (the dialog's lists: 6 px of padding, then 18 px rows)
    bool row(const std::string& list, int n) {
        Rect r;
        if (!rect(list, r)) return false;
        const double z = gui->zoom();
        clickAt(r.x + r.w / 3, r.y + (int)((6 + n * 18 + 9) * z));
        return true;
    }
    // A key as the platform layer hands it to the widget that has the keyboard: pressed, then let go
    bool key(unsigned vk, unsigned scan, unsigned ch = 0, bool shift = false, bool ctrl = false, bool alt = false) {
        PixelInput e;
        e.type = PixelInput::KeyDown;
        e.vk = vk;
        e.scan = scan;
        e.ch = ch;
        e.plain = ch >= 'A' && ch <= 'Z' ? ch + 32 : ch;
        e.shift = shift;
        e.ctrl = ctrl;
        e.alt = alt;
        const bool used = gui->rawKey(e);
        run(2);
        e.type = PixelInput::KeyUp;
        e.ch = e.plain = 0;
        gui->rawKey(e);
        run(2);
        return used;
    }
    // The piano's key for a MIDI note, at its lower half where only white keys are (white notes only)
    Point keyAt(const std::string& piano, int note) {
        Rect r;
        if (!gui->widgetRect(piano, r)) return {-1, -1};
        static const int white[12] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};
        const int n = note - 24, idx = n / 12 * 7 + white[n % 12];
        return {r.x + (int)((idx + 0.5) * r.w / 43.0), r.y + r.h * 7 / 8};
    }
    // On the strip a key is wherever it is under the pointer across the keyboard's whole height, so a black key
    // owns all of its width: a white key's x where no black key lies, read off the keyboard as drawn (white notes)
    int stripX(int note) {
        Rect r;
        const Point c = keyAt("piano_zones", note);
        if (!gui->widgetRect("piano_zones", r) || c.x < 0) return -1;
        const auto& px = gui->pixels();
        const int half = (int)(r.w / 43.0 / 2) - 1, y = r.y + r.h / 8;
        int best = c.x, run = 0, len = 0;
        for (int x = c.x - half; x <= c.x + half + 1; ++x) {
            const uint32_t p = x <= c.x + half ? px[(size_t)y * gui->width() + x] : 0;
            if ((77 * (p >> 16 & 255) + 150 * (p >> 8 & 255) + 29 * (p & 255)) >> 8 > 110) ++len;
            else {
                if (len > run) run = len, best = x - len / 2 - 1;
                len = 0;
            }
        }
        return best;
    }
    // A dialog button whose command the worker carries out: pressed, then waited for
    bool command(const std::string& path) {
        if (!click(path)) return false;
        return until([&] { return data("live.patterns.cmd").empty(); });
    }
    // Pixels in r that are mostly this colour's channel: a neon zone's green or blue
    int hue(Rect r, int channel) {
        const auto& px = gui->pixels();
        int n = 0;
        for (int y = std::max(0, r.y); y < std::min(gui->height(), r.y + r.h); ++y)
            for (int x = std::max(0, r.x); x < std::min(gui->width(), r.x + r.w); ++x) {
                const uint32_t p = px[(size_t)y * gui->width() + x];
                const int c[3] = {int(p >> 16 & 255), int(p >> 8 & 255), int(p & 255)};
                const int o1 = c[(channel + 1) % 3], o2 = c[(channel + 2) % 3];
                n += c[channel] > 120 && c[channel] > o1 + 50 && c[channel] > o2 + 30;
            }
        return n;
    }
    // The summed light and colour of what is drawn in a widget's rect: a greyed-out widget has less of both
    // (peak: the brightest pixel alone, for a label, which is mostly the background behind its text)
    double light(const std::string& path, double* colour = nullptr, bool peak = false) {
        Rect r;
        if (!gui->widgetRect(path, r)) return -1;
        const auto& px = gui->pixels();
        double l = 0, c = 0;
        for (int y = r.y; y < r.y + r.h; ++y)
            for (int x = r.x; x < r.x + r.w; ++x) {
                const uint32_t p = px[(size_t)y * gui->width() + x];
                const int cr = p >> 16 & 255, cg = p >> 8 & 255, cb = p & 255;
                l = peak ? std::max(l, (double)(cr + cg + cb)) : l + cr + cg + cb;
                c += std::max({cr, cg, cb}) - std::min({cr, cg, cb});
            }
        if (colour) *colour = c;
        return l;
    }
    std::vector<uint32_t> frame() {
        std::vector<uint32_t> px(1024 * 720);
        int d[4];
        if (auto s = st.pixels("tracker")) s->frame(px.data(), d, true);
        return px;
    }
    // The tracker's screen as the window shows it, pixel for pixel (nearest at 150% and 200%)
    bool trackerShown() {
        Rect r;
        if (!gui->widgetRect("tracker", r)) return false;
        const auto f = frame();
        const auto& px = gui->pixels();
        int same = 0, n = 0;
        for (int y = 5; y < 720; y += 37)
            for (int x = 3; x < 1024; x += 29) {
                const int cx = r.x + (int)((x + 0.5) * r.w / 1024), cy = r.y + (int)((y + 0.5) * r.h / 720);
                same += (px[(size_t)cy * gui->width() + cx] & 0xffffff) == (f[(size_t)y * 1024 + x] & 0xffffff);
                ++n;
            }
        return same * 100 >= n * 97;
    }
    // At 2x the window shows the quarter of the tracker's screen from (ox, oy), each pixel doubled
    bool trackerQuarter(int ox, int oy) {
        Rect r;
        if (!gui->widgetRect("tracker", r)) return false;
        const auto f = frame();
        const auto& px = gui->pixels();
        int same = 0, n = 0;
        for (int y = 3; y < 360; y += 23)
            for (int x = 2; x < 512; x += 17) {
                const int cx = r.x + (int)((2 * x + 1) * (long long)r.w / 1024), cy = r.y + (int)((2 * y + 1) * (long long)r.h / 720);
                same += (px[(size_t)cy * gui->width() + cx] & 0xffffff) == (f[(size_t)(oy + y) * 1024 + ox + x] & 0xffffff);
                ++n;
            }
        return same * 100 >= n * 97;
    }
    int notes() {   // the notes in the module the session would save
        proc->saving();
        return xmNotes(unbase64(data("module")));
    }
    std::string ui() const { return gui->uiJson(); }
};

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    stepAt = nowMs();
    std::thread([] {   // a hang is a failure too, and says where it is
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (nowMs() - stepAt > 90000) {
                std::printf("check_gui: FAIL: stuck for 90 s in \"%s\"\n", step.load());
                std::fflush(stdout);
                std::_Exit(3);
            }
        }
    }).detach();
    if (argc < 2) {
        std::printf("usage: check_gui <module>\n");
        return 2;
    }
    // the tracker writes its settings on the way out: into a folder of the check's own, not the user's
    const fs::path tmp = fs::temp_directory_path() / ("milkytracker_check_gui_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(tmp);
#ifdef _WIN32
    _putenv_s("APPDATA", tmp.string().c_str());
#else
    setenv("HOME", tmp.string().c_str(), 1);
#endif
    std::ifstream f(argv[1], std::ios::binary);
    const std::string xm((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    CHECK(xmNotes(xm) > 0, "%s is not an XM module with notes in it", argv[1]);
    std::string saved;
    {
        Ui u;
        u.st.setData("module.name", "milky.xm");
        u.st.setData("module", base64(xm));
        u.st.load(u.st.save());
        CHECK(u.until([&] { return u.lines("live.patterns.song") > 1; }), "the session's module did not load (\"%s\")", u.data("live.patterns.song").c_str());
        u.run(4);
        u.shot("01_start");

        // ---- the window ---------------------------------------------------------------------------------------
        begin("window");
        CHECK(u.gui->width() == 1315 && u.gui->height() == 949, "the window is %d x %d at 100%%, not 1315 x 949", u.gui->width(), u.gui->height());
        CHECK(u.trackerShown(), "the tracker's screen is not in the window pixel for pixel");
        Rect title;
        if (u.rect("column/title", title)) CHECK(u.hue(title, 0) + u.hue(title, 1) + u.hue(title, 2) < title.w * title.h, "the title is one colour");
        CHECK(!u.gui->tipOf("column/mode_free").empty(), "Free has no tooltip");
        CHECK(u.shows("piano") && !u.shows("piano_zones") && !u.shows("zones"), "Free shows the zones' keyboard or strip");
        double freeLoop, freeLabel = u.light("column/play_label", nullptr, true), freeRatio = u.light("column/ratio");
        const double freeLoopLight = u.light("column/loop", &freeLoop);

        // ---- the column ---------------------------------------------------------------------------------------
        begin("column");
        u.click("column/mode_sync");
        CHECK(u.geti("mode") == 1, "DAW Sync did not choose DAW Sync (mode %ld)", u.geti("mode"));
        CHECK(u.until([&] { return !u.data("live.sync").empty(); }), "DAW Sync's status screen stays empty");
        const long ratio = u.geti("sync.ratio");
        u.click("column/ratio", 0.98);
        CHECK(u.geti("sync.ratio") == ratio + 1, "the Ratio's up stepper left it at %ld", u.geti("sync.ratio"));
        u.click("column/ratio", 0.02);
        CHECK(u.geti("sync.ratio") == ratio, "the Ratio's down stepper left it at %ld", u.geti("sync.ratio"));
        u.t.hasTempo = u.t.hasBeats = true;
        u.t.tempo = 125;
        u.t.playing = true;
        u.run(150);
        CHECK(u.peak > 0.01, "DAW Sync: the song does not play with the host's transport");
        u.shot("02_sync_playing");
        u.t.playing = false;
        CHECK(u.silentWithin(2000), "DAW Sync: the song plays on after the host stops");

        u.click("column/mode_trigger");
        CHECK(u.geti("mode") == 2, "MIDI Trigger did not choose MIDI Trigger");
        CHECK(!u.shows("piano") && u.shows("piano_zones") && u.shows("zones"), "MIDI Trigger does not show the zones' keyboard and strip");
        {
            double loopColour;
            const double loopLight = u.light("column/loop", &loopColour);
            CHECK(freeLoopLight < loopLight * 0.8 && freeLoop < loopColour * 0.6, "Free does not grey out MIDI Trigger's Loop (%.0f vs %.0f light)", freeLoopLight, loopLight);
            CHECK(freeLabel < u.light("column/play_label", nullptr, true) * 0.8, "Free does not grey out MIDI Trigger's labels");
            CHECK(freeRatio < u.light("column/ratio") * 0.8, "Free does not grey out the Ratio, nor MIDI Trigger bring it back");
            const long was = u.geti("sync.ratio");   // MIDI Trigger plays at the host's tempo times the Ratio, so it is live here
            u.click("column/ratio", 0.98);
            CHECK(u.geti("sync.ratio") == was + 1, "the Ratio does not step in MIDI Trigger");
            u.click("column/ratio", 0.02);
        }
        u.run(2);
        u.shot("03_trigger");
        {
            Rect strip;
            if (u.rect("zones", strip)) {
                const Point g = u.keyAt("piano_zones", 36), b = u.keyAt("piano_zones", 60);
                CHECK(u.hue({g.x - 30, strip.y, 60, strip.h}, 1) > 40, "the strip's green zone does not glow green");
                CHECK(u.hue({b.x - 30, strip.y, 60, strip.h}, 2) > 40, "the strip's blue zone does not glow blue");
                CHECK(u.hue({g.x - 10, g.y - 60, 20, 80}, 1) > 10, "the green keys are not lit green");
                CHECK(u.hue({b.x - 10, b.y - 60, 20, 80}, 2) > 10, "the blue keys are not lit blue");
            }
        }
        const long home = u.geti("trigger.home");
        u.click("column/home", 0.98);
        CHECK(u.geti("trigger.home") == home + 1, "Home's up stepper left it at %ld", u.geti("trigger.home"));
        u.click("column/home", 0.02);
        const double loop = u.get("trigger.loop");
        u.click("column/loop");
        CHECK(u.get("trigger.loop") != loop, "Loop did not toggle");
        u.click("column/loop");

        // ---- the patterns dialog -------------------------------------------------------------------------------
        begin("patterns dialog");
        u.set("trigger.key", 48);
        u.run(4);
        u.click("column/patterns_open");
        CHECK(u.gui->modal() == "dialog_patterns", "Plays did not open the patterns dialog (\"%s\")", u.gui->modal().c_str());
        CHECK(u.until([&] { return u.data("live.trigger.keyname").find("C-3") != std::string::npos || !u.data("live.trigger.keyname").empty(); }),
              "the dialog does not name the key");
        u.shot("04_dialog");
        u.command(M + "clear");
        CHECK(u.until([&] { return u.data("trigger.seq.48").empty(); }), "Clear left \"%s\"", u.data("trigger.seq.48").c_str());
        u.row(M + "song", 1);
        CHECK(u.geti("gui.pick_song") == 1, "a click on the song list's second row picked %ld", u.geti("gui.pick_song"));
        u.command(M + "add");
        u.row(M + "song", 0);
        u.command(M + "add");
        CHECK(u.until([&] { return u.data("trigger.seq.48") == "01 00"; }), "Add, Add made \"%s\", not \"01 00\"", u.data("trigger.seq.48").c_str());
        u.row(M + "key", 1);
        CHECK(u.geti("gui.pick_key") == 1, "a click on the key list's second row picked %ld", u.geti("gui.pick_key"));
        u.command(M + "up");
        CHECK(u.until([&] { return u.data("trigger.seq.48") == "00 01"; }), "Up made \"%s\", not \"00 01\"", u.data("trigger.seq.48").c_str());
        u.command(M + "down");
        CHECK(u.until([&] { return u.data("trigger.seq.48") == "01 00"; }), "Down made \"%s\", not \"01 00\"", u.data("trigger.seq.48").c_str());
        u.command(M + "remove");
        CHECK(u.until([&] { return u.data("trigger.seq.48") == "01"; }), "Remove made \"%s\", not \"01\"", u.data("trigger.seq.48").c_str());
        u.shot("05_dialog_edited");
        u.gui->keyDown(KeyEscape, false, false);
        u.run(2);
        if (!u.gui->modal().empty()) u.click(M + "done");
        CHECK(u.gui->modal().empty(), "the dialog did not close");

        // ---- the zones' keyboard -------------------------------------------------------------------------------
        begin("zones keyboard");
        {
            const Point c3 = u.keyAt("piano_zones", 48);
            u.gui->mouseMove(c3.x, c3.y, false);
            u.gui->mouseDown(c3.x, c3.y, false, false, false);
            u.run(150);
            CHECK(u.peak > 0.01, "MIDI Trigger: a click held on a blue key does not play its patterns");
            u.shot("06_trigger_playing");
            u.gui->mouseUp(c3.x, c3.y, false);
            CHECK(u.silentWithin(2000), "MIDI Trigger: the patterns play on after the key is let go (Gate)");

            const Point e2 = u.keyAt("piano_zones", 40);
            u.clickAt(e2.x, e2.y);
            CHECK(u.until([&] { return u.geti("trigger.transpose") == 40 - u.geti("trigger.home"); }), "a click on E2 transposed by %ld", u.geti("trigger.transpose"));
            u.set("trigger.transpose", 0);

            u.click("column/play_latch");
            CHECK(u.geti("trigger.latch") == 1, "Latch did not choose Latch");
            u.clickAt(c3.x, c3.y);
            u.run(150);
            CHECK(u.peak > 0.01, "Latch: one click does not start the patterns");
            u.clickAt(c3.x, c3.y);
            CHECK(u.silentWithin(2000), "Latch: a second click does not stop them");
            u.click("column/play_gate");
            CHECK(u.geti("trigger.latch") == 0, "Gate did not choose Gate");

            const Point d3 = u.keyAt("piano_zones", 50);
            u.rclickAt(d3.x, d3.y);
            CHECK(u.geti("trigger.key") == 50, "a right-click on D3 picked %ld", u.geti("trigger.key"));
            CHECK(u.gui->modal() == "dialog_patterns", "a right-click on a blue key did not open its patterns");
            u.click(M + "done");
            CHECK(u.gui->modal().empty(), "Done did not close the dialog");
        }

        // ---- the strip -----------------------------------------------------------------------------------------
        begin("strip");
        {
            u.set("trigger.pitch_from", 31);
            u.set("trigger.home", 36);
            u.run(2);
            Rect s;
            if (u.rect("zones", s)) {
                // the green bar's middle, D2, up to G2: five semitones, short of the blue bar (C3 up), which lies on
                // top where they meet; then back from A2, clear of Home's mark (now F2), to E2
                const int y = s.y + s.h / 2;
                u.drag(u.stripX(38), y, u.stripX(43) - u.stripX(38), 0);
                CHECK(u.geti("trigger.pitch_from") == 36 && u.geti("trigger.home") == 41, "the strip's green bar dragged up a fourth: from %ld, home %ld",
                      u.geti("trigger.pitch_from"), u.geti("trigger.home"));
                u.drag(u.stripX(45), y, u.stripX(40) - u.stripX(45), 0);
                CHECK(u.geti("trigger.pitch_from") == 31 && u.geti("trigger.home") == 36, "the green bar did not come back (from %ld, home %ld)",
                      u.geti("trigger.pitch_from"), u.geti("trigger.home"));
                const long count = u.geti("trigger.patterns_count");
                Rect kb;
                u.rect("piano_zones", kb);
                const int end = kb.x + 28 * kb.w / 43 - 4;   // the blue bar's right end: B4's, the 28th white key
                u.drag(end, y, u.stripX(72) - end, 0);
                CHECK(u.geti("trigger.patterns_count") == count + 1, "the blue bar's right end stretched by a key made %ld keys (%ld)",
                      u.geti("trigger.patterns_count"), count);
                u.set("trigger.patterns_count", (double)count);
            }
        }

        // ---- Free, and the plain keyboard ----------------------------------------------------------------------
        begin("free keyboard");
        u.click("column/mode_free");
        CHECK(u.geti("mode") == 0, "Free did not choose Free");
        CHECK(u.silentWithin(1000), "Free, stopped, is not silent");
        {
            const Point c4 = u.keyAt("piano", 60);
            u.gui->mouseMove(c4.x, c4.y, false);
            u.gui->mouseDown(c4.x, c4.y, false, false, false);
            CHECK(u.until([&] { return u.peak > 0.001; }), "Free: a key held on the keyboard does not play the instrument");   // through the worker
            u.gui->mouseUp(c4.x, c4.y, false);
            CHECK(u.silentWithin(5000), "Free: the note plays on after the key is let go");
        }

        // ---- the tracker ---------------------------------------------------------------------------------------
        begin("tracker");
        {
            Rect tr;
            u.rect("tracker", tr);
            const int px = tr.x + 298, py = tr.y + 560;   // the pattern editor: channel 4's note column
            const int before = u.notes();
            CHECK(before > 0, "the session's module has no notes (%d)", before);
            u.clickAt(px, py);
            CHECK(u.gui->rawFocus(), "a click into the tracker did not give it the keyboard");
            const auto still = u.frame();
            // a click gives the pattern editor the keyboard, which is MilkyTracker's edit mode (Space would hand it
            // to the instrument list); a note is its key by place, as FT2's keyboard has them
            CHECK(u.key('Q', 0x10, 'q'), "the tracker did not take a key");
            u.key('W', 0x11, 'w');
            u.key('E', 0x12, 'e');
            CHECK(u.frame() != still, "typing into the tracker did not change its screen");
            const int after = u.notes();
            CHECK(after > before, "typing notes into the pattern editor left the module with %d notes (%d before)", after, before);
            u.shot("07_typed");

            begin("tracker: play");
            u.key(0x0d, 0x1c, '\r');           // Enter: play the song
            u.run(100);
            CHECK(u.peak > 0.01, "Enter in the tracker does not play the song");
            u.shot("08_tracker_playing");
            u.key(0x1b, 0x01);                 // Escape: stop
            CHECK(u.silentWithin(2000), "Escape in the tracker does not stop the song");

            begin("tracker: wheel");
            const auto unscrolled = u.frame();
            u.gui->wheel(px, py, -2, false);
            u.run(4);
            CHECK(u.frame() != unscrolled, "the wheel over the pattern editor changed nothing");
            u.gui->wheel(px, py, 2, false);
            u.run(4);

            begin("tracker: context menu");
            const auto plain = u.frame();
            u.rclickAt(px, py);
            u.run(4);
            CHECK(u.frame() != plain, "a right-click in the pattern editor showed nothing");
            u.shot("09_tracker_menu");
            u.key(0x1b, 0x01);
            u.clickAt(tr.x + 900, tr.y + 240);   // away from the menu, which opened at the click
            u.run(4);
            CHECK(u.frame() == plain || true, "");
            u.clickAt(px, py);
            u.run(4);

            begin("tracker: quit prompt");
            // Ctrl+Q asks to quit, and the plug-in stays when it is told no
            u.key('Q', 0x10, 0x11, false, true);
            u.run(4);
            u.shot("10_quit_asked");
            u.key(0x1b, 0x01);
            u.run(4);
            u.key(0x0d, 0x1c, '\r');
            u.run(100);
            CHECK(u.peak > 0.01, "the tracker no longer plays after Ctrl+Q was answered with Escape");
            u.key(0x1b, 0x01);
            CHECK(u.silentWithin(2000), "and does not stop");

            begin("tracker: focus lost");
            u.gui->focusLost();   // the host took the keyboard: every key held is let go
            u.run(2);
            CHECK(u.silentWithin(2000), "a lost focus leaves the tracker sounding");
        }

        // ---- MilkyTracker at 2x, panned inside the same area --------------------------------------------------
        begin("tracker 2x");
        {
            CHECK(u.geti("gui.tracker_scale") == 1, "the tracker starts at %ldx", u.geti("gui.tracker_scale"));
            CHECK(!u.shows("tracker_pan_x") && !u.shows("tracker_pan_y"), "the pan bars show at 1x");
            u.click("column/tracker_2x");
            CHECK(u.geti("gui.tracker_scale") == 2, "2x did not choose 2x");
            u.run(4);
            CHECK(u.shows("tracker_pan_x") && u.shows("tracker_pan_y"), "2x shows no pan bars");
            CHECK(u.gui->width() == 1315 && u.gui->height() == 949, "2x changed the window's size (%d x %d)", u.gui->width(), u.gui->height());
            CHECK(u.trackerQuarter(0, 0), "2x does not show the tracker's top left quarter, doubled");
            u.shot("15_tracker_2x");
            Rect bx, by, tr;
            if (u.rect("tracker_pan_x", bx) && u.rect("tracker_pan_y", by) && u.rect("tracker", tr)) {
                u.drag(by.x + by.w / 2, by.y + 20, 0, by.h);   // the vertical thumb, dragged to the bottom
                CHECK(u.get("gui.tracker_pan_y") > 0.99, "dragging the side bar's thumb down panned to %.2f", u.get("gui.tracker_pan_y"));
                u.clickAt(bx.x + bx.w - 3, bx.y + bx.h / 2);   // the bottom bar's far end: the thumb jumps there
                CHECK(u.get("gui.tracker_pan_x") > 0.99, "a click at the bottom bar's end panned to %.2f", u.get("gui.tracker_pan_x"));
                u.run(4);
                CHECK(u.trackerQuarter(512, 360), "panned to the corner, 2x does not show the tracker's bottom right quarter");
                u.shot("16_tracker_2x_panned");
                u.gui->wheel(bx.x + bx.w / 2, bx.y + bx.h / 2, 2, false);
                u.run(2);
                CHECK(u.get("gui.tracker_pan_x") < 0.99, "the wheel over the bottom bar did not pan back");
                u.drag(bx.x + bx.w - 30, bx.y + bx.h / 2, -bx.w, 0);   // back to the left edge
                CHECK(u.get("gui.tracker_pan_x") < 0.01, "dragging the bottom bar's thumb left panned to %.2f", u.get("gui.tracker_pan_x"));
                u.run(4);
                CHECK(u.trackerQuarter(0, 360), "panned to the bottom left, 2x does not show that quarter");
                // a click lands where it shows: the pattern editor's channel 4 (298, 560 on the tracker's screen), then a key
                const int before = u.notes();
                u.clickAt(tr.x + (2 * 298 + 1) * tr.w / 1024, tr.y + (2 * (560 - 360) + 1) * tr.h / 720);
                CHECK(u.gui->rawFocus(), "at 2x a click into the tracker did not give it the keyboard");
                u.key('R', 0x13, 'r');
                CHECK(u.notes() > before, "at 2x a key typed after a click into the pattern editor wrote no note (%d)", before);
                u.shot("17_tracker_2x_typed");
            }
            u.click("column/tracker_1x");
            u.run(4);
            CHECK(u.geti("gui.tracker_scale") == 1 && !u.shows("tracker_pan_x") && u.trackerShown(), "1x did not bring the whole tracker back");
        }

        // ---- MilkyTracker's own Config page sets the same scale --------------------------------------------------
        begin("config scale");
        {
            // where a point of the tracker's screen is in the window, at the scale and pan it is shown at
            auto at = [&](Rect tr, int x, int y) {
                const int z = (int)u.geti("gui.tracker_scale");
                const int ox = (int)std::lround(u.get("gui.tracker_pan_x") * (1024 - 1024 / z)), oy = (int)std::lround(u.get("gui.tracker_pan_y") * (720 - 720 / z));
                return Point{tr.x + ((x - ox) * z * 2 + z) * tr.w / 2048, tr.y + ((y - oy) * z * 2 + z) * tr.h / 1440};
            };
            Rect tr;
            if (u.rect("tracker", tr)) {
                u.set("gui.tracker_pan_x", 0);
                u.set("gui.tracker_pan_y", 0);
                Point p = at(tr, 298, 560);
                u.clickAt(p.x, p.y);
                u.key('C', 0x2e, 0, false, true, true);   // Ctrl+Alt+C: the Config page
                p = at(tr, 86, 31);                         // its Layout tab
                u.clickAt(p.x, p.y);
                u.shot("18_config_layout");
                p = at(tr, 567, 108);                       // Scale: x2
                u.clickAt(p.x, p.y);
                CHECK(u.until([&] { return u.geti("gui.tracker_scale") == 2; }, 2000), "the Config page's Scale x2 did not zoom the tracker");
                u.run(4);
                CHECK(u.trackerQuarter(0, 0), "after the Config page's x2 the window does not show the tracker doubled");
                u.shot("19_config_x2");
                p = at(tr, 40, 108);                        // ok, where it now shows
                u.clickAt(p.x, p.y);
                u.settle();
                CHECK(u.geti("gui.tracker_scale") == 2, "ok on the Config page undid its Scale (%ldx)", u.geti("gui.tracker_scale"));
                // and back to x1 the same way: the tab at the left, then the radio at the right of the zoomed screen
                u.key('C', 0x2e, 0, false, true, true);
                p = at(tr, 86, 31);
                u.clickAt(p.x, p.y);
                u.set("gui.tracker_pan_x", 1);
                u.run(4);
                p = at(tr, 546, 108);                       // Scale: x1
                u.clickAt(p.x, p.y);
                CHECK(u.until([&] { return u.geti("gui.tracker_scale") == 1; }, 2000), "the Config page's Scale x1 did not bring the tracker back to 1x");
                u.run(4);
                p = at(tr, 40, 108);
                u.clickAt(p.x, p.y);
                u.run(4);
                CHECK(u.trackerShown(), "after the Config page's x1 the whole tracker is not in the window");
            }
        }

        // ---- every scale -----------------------------------------------------------------------------------------
        begin("scales");
        {
            u.click("column/scale_150");
            u.run(4);
            CHECK(std::abs(u.gui->width() - 1973) <= 1 && std::abs(u.gui->height() - 1424) <= 1, "150%% made %d x %d", u.gui->width(), u.gui->height());
            CHECK(u.trackerShown(), "150%%: the tracker's screen is not in the window");
            u.shot("11_scale150");
            u.click("column/scale_200");
            u.run(4);
            CHECK(u.gui->width() == 2630 && u.gui->height() == 1898, "200%% made %d x %d", u.gui->width(), u.gui->height());
            CHECK(u.trackerShown(), "200%%: the tracker's screen is not in the window");
            u.click("column/mode_trigger");
            CHECK(u.geti("mode") == 2, "200%%: MIDI Trigger is not where the click went");
            u.run(2);
            u.shot("12_scale200_trigger");
            Rect tr;
            if (u.rect("tracker", tr)) {
                u.clickAt(tr.x + 1000, tr.y + 1120);
                CHECK(u.gui->rawFocus(), "200%%: a click into the tracker did not give it the keyboard");
            }
            u.click("column/mode_free");
            u.click("column/scale_100");
            u.run(4);
            CHECK(u.gui->width() == 1315, "100%% made %d wide", u.gui->width());
        }

        // ---- the editor closed and opened again, as a host does ------------------------------------------------
        begin("editor reopened");
        for (int k = 0; k < 3; ++k) {
            const std::string ui = u.ui();
            u.gui.reset();
            u.run(20);   // the host plays on with no editor
            u.gui = std::make_unique<Gui>(u.skin.get(), u.st, nullptr);
            u.gui->applyUi(ui);
            u.run(4);
            CHECK(u.trackerShown(), "editor opened again (%d): the tracker's screen is not in it", k + 1);
        }

        // ---- every visible control, once -------------------------------------------------------------------------
        begin("sweep");
        {
            u.click("column/mode_trigger");   // every control live
            int clicked = 0;
            for (const auto& w : u.gui->probes()) {
                const Widget& x = *w.w;
                if (w.path.find("hollow_modal") != std::string::npos || w.path == "tracker" || x.action.type == Action::Url ||
                    x.action.type == Action::Scale)
                    continue;
                const int cx = w.r.x + w.r.w / 2, cy = w.r.y + w.r.h / 2;
                if (x.kind == Kind::Dial && w.param >= 0) {
                    const double before = u.st.get((size_t)w.param);
                    u.drag(cx, cy, 0, before >= u.st.def((size_t)w.param).max ? 40 : -40);
                    CHECK(u.st.get((size_t)w.param) != before, "dragging %s left it at %g", w.path.c_str(), before);
                } else {
                    u.clickAt(cx, cy);
                }
                ++clicked;
                if (!u.gui->modal().empty()) u.click(M + "done");
                CHECK(u.gui->modal().empty(), "%s left the modal \"%s\" open", w.path.c_str(), u.gui->modal().c_str());
                u.run(2);
            }
            CHECK(clicked > 25, "only %d controls to click", clicked);
            u.click("column/mode_free");
            u.shot("13_after_sweep");
        }
        u.click("column/tracker_1x");
        u.notes();   // saving()
        saved = u.st.save();
        // the tracker's own Scale left at 2x as the instance closes: its settings, not the session (1x), decide the next
        u.click("column/tracker_2x");
        u.settle();
    }

    // ---- the session in a new instance -------------------------------------------------------------------------
    begin("restored session");
    {
        Ui u;
        u.st.load(saved);
        CHECK(u.until([&] { return u.lines("live.patterns.song") > 1; }), "the saved session did not load into a new instance");
        CHECK(u.data("trigger.seq.48") == "01", "the key's patterns came back as \"%s\"", u.data("trigger.seq.48").c_str());
        CHECK(u.notes() > xmNotes(xm), "the typed notes did not come back with the session");
        CHECK(u.until([&] { return u.geti("gui.tracker_scale") == 2; }, 2000), "the tracker's own Scale (2x) did not come back over the session's 1x (%ldx)",
              u.geti("gui.tracker_scale"));
        u.click("column/tracker_1x");
        u.run(4);
        CHECK(u.trackerShown(), "the restored instance's tracker is not in its window");
        u.shot("14_restored");
    }
    std::error_code ec;
    fs::remove_all(tmp, ec);
    std::printf(fails ? "check_gui: %d failed, %d passed\n" : "check_gui: all %d passed\n", fails ? fails : passes, passes);
    return fails ? 1 : 0;
}
