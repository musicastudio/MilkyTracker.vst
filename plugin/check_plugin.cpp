// The processor driven the way a host drives it, without a host: a session's module comes back, the tracker's
// screen draws, DAW Sync starts and stops the song with the transport, and MIDI Trigger plays a key's patterns,
// its pitch keys transpose, the patterns dialog and the zone strip edit what they show, and a save puts the module
// back into the session.
//   check_plugin <a module, such as resources/music/milky.xm>
#include <hollow/hollow.h>
#include "core/core.h"   // the GUI without a window, as Hollow's own checks drive it
#include "checks.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

using namespace hollow;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

static void wait(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) {
        std::printf("usage: check_plugin <module>\n");
        return 2;
    }
#ifdef _WIN32
    // the tracker writes its settings on the way out: into a folder of the check's own, not the user's
    const std::string dir = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "\\milkytracker_check";
    _putenv_s("APPDATA", dir.c_str());
#endif
    std::ifstream f(argv[1], std::ios::binary);
    const std::string xm((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    check(xm.size() > 1000, "the module reads");

    State state(skinParams(*loadSkin()));
    auto proc = createProcessor(state);
    proc->prepare(48000, 512);
    const int mode = state.indexOf("mode"), ratio = state.indexOf("sync.ratio"), transpose = state.indexOf("trigger.transpose");

    std::vector<float> l(512), r(512);
    float* out[2] = {l.data(), r.data()};
    Transport t;
    double peak = 0;
    auto run = [&](int blocks) {
        peak = 0;
        for (int b = 0; b < blocks; ++b) {
            std::fill(l.begin(), l.end(), 0.0f);
            std::fill(r.begin(), r.end(), 0.0f);
            proc->transport = t;
            proc->process(nullptr, out, 512);
            for (int i = 0; i < 512; ++i) peak = std::max(peak, (double)std::fabs(l[i]) + std::fabs(r[i]));
            if (t.playing) t.beats += 512.0 / 48000 * t.tempo / 60;
        }
    };

    // a session: the module in the state's text data, then a load, which the worker answers
    state.setData("module.name", "milky.xm");
    state.setData("module", base64(xm));
    state.load(state.save());
    wait(500);

    std::shared_ptr<Pixels> screen = state.pixels("tracker");
    check(screen && screen->width() == 1024 && screen->height() == 720, "the tracker's screen is 1024 x 720");
    if (screen) {
        std::vector<uint32_t> px((size_t)screen->width() * screen->height());
        int d[4];
        screen->frame(px.data(), d, true);
        size_t lit = 0;
        for (uint32_t p : px) lit += (p & 0xffffff) != 0;
        check(lit > px.size() / 4, "the tracker draws its screen");
        PixelInput e;
        e.type = PixelInput::Move;
        e.x = 500;
        e.y = 300;
        screen->input(e);
        screen->tick();
    }

    run(20);
    check(peak == 0, "Free, stopped: silence");

    state.set((size_t)mode, 1);   // DAW Sync
    state.set((size_t)ratio, 3);  // x1
    t.hasTempo = t.hasBeats = true;
    t.tempo = 125;
    t.playing = true;
    run(200);
    check(peak > 0.01, "DAW Sync: the song plays with the host's transport");
    t.playing = false;
    run(20);
    run(20);
    check(peak == 0, "DAW Sync: and stops with it");
    // a host whose first playing block has no position yet (clap-wrapper's AU at beat 0 sent none): the song still starts
    t.playing = true;
    t.hasBeats = false;
    run(1);
    t.hasBeats = true;
    run(200);
    check(peak > 0.01, "DAW Sync: the song starts when the host's first playing block has no position");
    t.playing = false;
    run(20);
    run(20);
    // a host prepares again whenever its rate or block size changes, and the song plays on at the new rate
    proc->prepare(44100, 64);
    t.playing = true;
    run(200);
    check(peak > 0.01, "DAW Sync: the song plays after the host prepares again at 44.1 kHz");
    t.playing = false;
    run(20);
    run(20);
    proc->prepare(48000, 512);
    // a session loaded while the host plays (a project opened mid-playback): the song comes back where the host is
    t.playing = true;
    run(50);
    state.load(state.save());
    for (int i = 0; i < 40; ++i) {
        run(4);
        wait(10);
    }
    run(100);
    check(peak > 0.01, "DAW Sync: a session loaded while the host plays comes back playing");
    t.playing = false;
    run(20);
    run(20);

    state.set((size_t)mode, 2);   // MIDI Trigger
    state.setData("trigger.seq.48", "00");
    wait(100);
    const uint8_t on[3] = {0x90, 48, 100}, off[3] = {0x80, 48, 0}, pitch[3] = {0x90, 40, 100};
    proc->midi(0, on, 3);
    run(200);
    check(peak > 0.01, "MIDI Trigger: a pattern key plays its patterns");
    proc->midi(0, off, 3);
    run(20);
    run(20);
    check(peak == 0, "MIDI Trigger: and stops when the key is let go (Gate)");
    proc->midi(0, pitch, 3);
    run(1);
    check(std::lround(state.get((size_t)transpose)) == 4, "MIDI Trigger: E2, four above the home C2, transposes by +4");

    // the patterns dialog: its buttons leave a command the worker carries out on the picked key's list
    const int pickSong = state.indexOf("gui.pick_song"), pickKey = state.indexOf("gui.pick_key");
    state.set((size_t)state.indexOf("trigger.key"), 48);
    wait(50);
    check(state.data("live.patterns.song").find("01\t") != std::string::npos, "the dialog lists the song's patterns");
    auto command = [&](const char* cmd) {
        state.setData("live.patterns.cmd", cmd);
        for (int i = 0; i < 100 && !state.data("live.patterns.cmd").empty(); ++i) wait(5);
        wait(20);
    };
    state.set((size_t)pickSong, 1);
    command("add");
    check(state.data("trigger.seq.48") == "00 01", "Add puts the picked pattern at the end of the key's list");
    state.set((size_t)pickKey, 1);
    command("up");
    check(state.data("trigger.seq.48") == "01 00", "Up plays the picked place sooner");
    state.set((size_t)pickKey, 0);
    command("remove");
    check(state.data("trigger.seq.48") == "00" && state.data("live.trigger.edit") == "00", "Remove takes it off");

    // the zone strip under the keys, driven as a mouse drives it (1x: the strip is [58, 919, 1198, 20], C1 at x 58,
    // 195 px an octave): the green bar moved an octave up takes Home with it, the blue bar's end stretched to C5,
    // the blue mark dragged to E3
    {
        const int pitchFrom = state.indexOf("trigger.pitch_from"), home = state.indexOf("trigger.home");
        const int patCount = state.indexOf("trigger.patterns_count"), key = state.indexOf("trigger.key");
        state.set((size_t)pitchFrom, 31);
        state.set((size_t)home, 36);
        state.set((size_t)key, 48);
        auto skin = loadSkin();
        Gui gui(skin.get(), state, nullptr);
        auto drag = [&](int x0, int x1) {
            gui.mouseDown(x0, 929, false, false, false);
            gui.mouseMove(x1, 929, false);
            gui.mouseUp(x1, 929, false);
        };
        drag(300, 300 + 195);   // D2, between Home's mark and the bar's ends, to D3
        check(std::lround(state.get((size_t)pitchFrom)) == 43 && std::lround(state.get((size_t)home)) == 48,
              "the strip: the green zone moves an octave, Home with it");
        drag(834, 850);         // the blue bar's right end, B4, to C5
        check(std::lround(state.get((size_t)patCount)) == 25, "the strip: the blue zone stretches to C5");
        drag(657, 715);         // the blue mark, C3, to E3
        check(std::lround(state.get((size_t)key)) == 52, "the strip: the picked key's mark moves to E3");
    }

    proc->saving();
    check(state.data("module").size() > 1000 && !state.data("module.name").empty(), "saving puts the module into the session");

    proc.reset();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
