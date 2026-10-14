// check_host: the plug-in as a host loads it. A built binary, CLAP, VST3, VST2 or (on macOS) AU, is opened through its
// own API with an audio thread running it in real time beside the main thread, as a DAW runs it: the session's module
// put in through the format's own state, the host's transport starting and stopping DAW Sync, MIDI notes playing MIDI Trigger
// and Free, a second instance beside the first, other sample rates and block sizes, and the editor in a real window of
// the platform's own (Win32, X11 or Cocoa): its picture captured, clicks and keys sent as the platform's own events (a
// mode chosen, a note typed into the tracker), its size changed through the format's resize, closed and opened again.
// "standalone" starts the standalone app instead, captures its window and closes it as a user would.
// Exits non-zero on any failure.
//   check_host clap|vst3|vst2|au <plug-in> <module.xm> [<folder for screenshots>]   (au: macOS)
//   check_host standalone <app> [<folder for screenshots>]
// With no display to open a window on (Linux without DISPLAY), the editor's checks are skipped, and it says so.
#include "checks.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef MT_CLAP_HOST
#include <clap/clap.h>
#endif
#include "vst2/vst2.h"
#ifdef MT_VST3_HOST
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#endif

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#import <AudioToolbox/AudioToolbox.h>
#import <AudioUnit/AUCocoaUIView.h>
#import <Cocoa/Cocoa.h>
#include <cstddef>
#include <dlfcn.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <dlfcn.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "stb/stb_image_write.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

static int fails = 0, passes = 0;
#define CHECK(c, ...) do { if (c) ++passes; else { std::printf("check_host: FAIL line %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); ++fails; } } while (0)
static void say(const char* what) { std::printf("check_host: %s\n", what); }
static std::string shots;   // the folder screenshots go to, "" for none

static void writePng(const std::string& name, const std::vector<uint32_t>& px, int w, int h) {
    if (shots.empty() || px.empty()) return;
    std::vector<uint8_t> rgba;
    rgba.reserve(px.size() * 4);
    for (uint32_t p : px) rgba.insert(rgba.end(), {uint8_t(p >> 16), uint8_t(p >> 8), uint8_t(p), 255});
    fs::create_directories(shots);
    stbi_write_png((fs::path(shots) / (name + ".png")).string().c_str(), w, h, 4, rgba.data(), w * 4);
}

// How much of a picture is drawn: the share of pixels that are not near black, and how many colours it has
static void drawn(const std::vector<uint32_t>& px, double& lit, int& colours) {
    std::vector<uint32_t> seen;
    size_t n = 0;
    for (size_t i = 0; i < px.size(); i += 7) {
        const uint32_t p = px[i] & 0xffffff;
        n += ((p >> 16 & 255) + (p >> 8 & 255) + (p & 255)) > 60;
        if (seen.size() < 4096 && std::find(seen.begin(), seen.end(), p) == seen.end()) seen.push_back(p);
    }
    lit = px.empty() ? 0 : (double)n / (double)((px.size() + 6) / 7);
    colours = (int)seen.size();
}

// ---- the session's text, as Hollow's State saves it -------------------------------------------------------------
// "hollow-state 1", then a line a param ("param <id> <value>"), a line a text data key ("data [<key>,<value>]",
// JSON strings) and the editor's "ui" line. A format may carry it with more around it, so it is found, not assumed.
static size_t blobAt(const std::string& s) { return s.find("hollow-state 1\n"); }
static std::string withParam(std::string s, const std::string& id, double v) {
    const std::string key = "\nparam " + id + " ";
    size_t a = s.find(key);
    if (a == std::string::npos) return s;
    a += key.size();
    const size_t e = s.find('\n', a);
    char num[32];
    std::snprintf(num, sizeof num, "%.17g", v);
    return s.replace(a, e - a, num);
}
static std::string withData(std::string s, const std::string& key, const std::string& value) {   // value: no quotes or backslashes
    const std::string tag = "\ndata [\"" + key + "\",\"";
    size_t a = s.find(tag);
    if (a != std::string::npos) {
        const size_t e = s.find('\n', a + 1);
        return s.replace(a + 1, e - a - 1, "data [\"" + key + "\",\"" + value + "\"]");
    }
    a = s.find("\nui ");
    if (a == std::string::npos) a = s.size() - 1;
    return s.insert(a + 1, "data [\"" + key + "\",\"" + value + "\"]\n");
}
static std::string dataOf(const std::string& s, const std::string& key) {
    const std::string tag = "\ndata [\"" + key + "\",\"";
    const size_t a = s.find(tag);
    if (a == std::string::npos) return "";
    const size_t b = a + tag.size(), e = s.find("\"]", b);
    return e == std::string::npos ? "" : s.substr(b, e - b);
}
static double paramOf(const std::string& s, const std::string& id) {
    const std::string key = "\nparam " + id + " ";
    const size_t a = s.find(key);
    return a == std::string::npos ? -1e9 : std::atof(s.c_str() + a + key.size());
}

// ---- a format's instance ------------------------------------------------------------------------------------------
struct Note { uint8_t b[3]; };
struct Plug {
    virtual ~Plug() = default;
    virtual bool activate(double rate, int block) = 0;   // main thread
    virtual void deactivate() = 0;                       // main thread
    virtual void startAudio() {}                         // audio thread, before its first block
    virtual void stopAudio() {}                          // audio thread, after its last
    virtual void process(float* l, float* r, int n, const std::vector<Note>& notes, bool playing, double tempo, double beats, int64_t pos) = 0;
    virtual std::string save() = 0;                      // main thread
    virtual bool load(const std::string& s) = 0;         // main thread
    virtual bool openEditor(void* parent, int& w, int& h) = 0;
    virtual void closeEditor() = 0;
    virtual void idle() {}                               // main thread, between everything else
    std::atomic<int> wantW{0}, wantH{0};                 // the size it last asked its window to be
    std::string name;
};

static std::thread::id mainThread;
static thread_local bool onAudio = false;   // this thread is an instance's audio thread

// ---- loading a binary -----------------------------------------------------------------------------------------------
struct Lib {
    void* h = nullptr;
    explicit Lib(const std::string& file) {
#ifdef _WIN32
        h = (void*)LoadLibraryW(fs::u8path(file).wstring().c_str());
#else
        h = dlopen(file.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!h) std::printf("check_host: %s\n", dlerror());
#endif
    }
    ~Lib() {
#ifdef _WIN32
        if (h) FreeLibrary((HMODULE)h);
#else
        if (h) dlclose(h);
#endif
    }
    void* sym(const char* name) const {
#ifdef _WIN32
        return h ? (void*)GetProcAddress((HMODULE)h, name) : nullptr;
#else
        return h ? dlsym(h, name) : nullptr;
#endif
    }
};

// The binary inside a macOS bundle (Contents/MacOS/<name>); elsewhere the file itself
static std::string binaryOf(const std::string& path) {
#ifdef __APPLE__
    const fs::path dir = fs::path(path) / "Contents" / "MacOS";
    std::error_code ec;
    if (fs::is_directory(dir, ec))
        for (auto& e : fs::directory_iterator(dir)) return e.path().string();
#endif
    return path;
}

// ---- CLAP -----------------------------------------------------------------------------------------------------------
#ifdef MT_CLAP_HOST
struct ClapPlug : Plug {
    std::unique_ptr<Lib> lib;
    const clap_plugin_entry_t* entry = nullptr;
    const clap_plugin_t* p = nullptr;
    const clap_plugin_state_t* state = nullptr;
    const clap_plugin_gui_t* gui = nullptr;
    clap_host_t host{};
    std::atomic<bool> callback{false};
    bool active = false, editor = false;
    std::string api;
    clap_event_transport_t transport{};
    std::vector<clap_event_midi_t> events;

    static ClapPlug* of(const clap_host_t* h) { return (ClapPlug*)h->host_data; }

    explicit ClapPlug(const std::string& path) {
        lib = std::make_unique<Lib>(binaryOf(path));
        entry = (const clap_plugin_entry_t*)lib->sym("clap_entry");
        if (!entry || !entry->init(path.c_str())) return;
        auto* factory = (const clap_plugin_factory_t*)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
        if (!factory || factory->get_plugin_count(factory) < 1) return;
        const clap_plugin_descriptor_t* d = factory->get_plugin_descriptor(factory, 0);
        name = d->name;
        host.clap_version = CLAP_VERSION;
        host.host_data = this;
        host.name = "check_host";
        host.vendor = "musica.studio";
        host.url = "";
        host.version = "1";
        host.get_extension = [](const clap_host_t*, const char* id) -> const void* {
            static const clap_host_thread_check_t threads = {
                [](const clap_host_t*) { return std::this_thread::get_id() == mainThread; },
                [](const clap_host_t*) { return onAudio; }};
            static const clap_host_gui_t gui = {
                [](const clap_host_t*) {},
                [](const clap_host_t* h, uint32_t w, uint32_t hh) { of(h)->wantW = (int)w; of(h)->wantH = (int)hh; return true; },
                [](const clap_host_t*) { return true; },
                [](const clap_host_t*) { return true; },
                [](const clap_host_t*, bool) {}};
            static const clap_host_params_t params = {
                [](const clap_host_t*, clap_param_rescan_flags) {},
                [](const clap_host_t*, clap_id, clap_param_clear_flags) {},
                [](const clap_host_t*) {}};
            static const clap_host_state_t state = {[](const clap_host_t*) {}};
            static const clap_host_log_t log = {[](const clap_host_t*, clap_log_severity s, const char* m) { std::printf("check_host: plug-in log %d: %s\n", (int)s, m); }};
            if (!std::strcmp(id, CLAP_EXT_THREAD_CHECK)) return &threads;
            if (!std::strcmp(id, CLAP_EXT_GUI)) return &gui;
            if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &params;
            if (!std::strcmp(id, CLAP_EXT_STATE)) return &state;
            if (!std::strcmp(id, CLAP_EXT_LOG)) return &log;
            return nullptr;
        };
        host.request_restart = [](const clap_host_t*) {};
        host.request_process = [](const clap_host_t*) {};
        host.request_callback = [](const clap_host_t* h) { of(h)->callback = true; };
        p = factory->create_plugin(factory, &host, d->id);
        if (!p || !p->init(p)) {
            p = nullptr;
            return;
        }
        state = (const clap_plugin_state_t*)p->get_extension(p, CLAP_EXT_STATE);
        gui = (const clap_plugin_gui_t*)p->get_extension(p, CLAP_EXT_GUI);
    }
    ~ClapPlug() override {
        if (p) {
            if (editor) closeEditor();
            if (active) deactivate();
            p->destroy(p);
        }
        if (entry) entry->deinit();
    }
    bool activate(double rate, int block) override { return active = p->activate(p, rate, 1, (uint32_t)block); }
    void deactivate() override {
        p->deactivate(p);
        active = false;
    }
    void startAudio() override { p->start_processing(p); }
    void stopAudio() override { p->stop_processing(p); }
    void process(float* l, float* r, int n, const std::vector<Note>& notes, bool playing, double tempo, double beats, int64_t pos) override {
        events.clear();
        for (const Note& m : notes) {
            clap_event_midi_t e{};
            e.header = {sizeof e, 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0};
            e.port_index = 0;
            std::memcpy(e.data, m.b, 3);
            events.push_back(e);
        }
        clap_input_events_t in{this,
                               [](const clap_input_events_t* l) { return (uint32_t)((ClapPlug*)l->ctx)->events.size(); },
                               [](const clap_input_events_t* l, uint32_t i) -> const clap_event_header_t* { return &((ClapPlug*)l->ctx)->events[i].header; }};
        clap_output_events_t out{this, [](const clap_output_events_t*, const clap_event_header_t*) { return true; }};
        transport = {};
        transport.header = {sizeof transport, 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0};
        transport.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE | (playing ? CLAP_TRANSPORT_IS_PLAYING : 0);
        transport.tempo = tempo;
        transport.song_pos_beats = (clap_beattime)std::llround(beats * CLAP_BEATTIME_FACTOR);
        transport.tsig_num = transport.tsig_denom = 4;
        float* ch[2] = {l, r};
        clap_audio_buffer_t ob{};
        ob.data32 = ch;
        ob.channel_count = 2;
        clap_process_t pr{};
        pr.steady_time = pos;
        pr.frames_count = (uint32_t)n;
        pr.transport = &transport;
        pr.audio_outputs = &ob;
        pr.audio_outputs_count = 1;
        pr.in_events = &in;
        pr.out_events = &out;
        p->process(p, &pr);
    }
    std::string save() override {
        std::string s;
        clap_ostream_t o{&s, [](const clap_ostream_t* o, const void* b, uint64_t n) -> int64_t { ((std::string*)o->ctx)->append((const char*)b, (size_t)n); return (int64_t)n; }};
        return state && state->save(p, &o) ? s : "";
    }
    bool load(const std::string& s) override {
        struct In { const std::string& s; size_t at; } in{s, 0};
        clap_istream_t i{&in, [](const clap_istream_t* i, void* b, uint64_t n) -> int64_t {
                             In& r = *(In*)i->ctx;
                             const size_t k = std::min<size_t>((size_t)n, r.s.size() - r.at);
                             std::memcpy(b, r.s.data() + r.at, k);
                             r.at += k;
                             return (int64_t)k;
                         }};
        return state && state->load(p, &i);
    }
    bool openEditor(void* parent, int& w, int& h) override {
#ifdef _WIN32
        api = CLAP_WINDOW_API_WIN32;
#elif defined(__APPLE__)
        api = CLAP_WINDOW_API_COCOA;
#else
        api = CLAP_WINDOW_API_X11;
#endif
        if (!gui || !gui->is_api_supported(p, api.c_str(), false) || !gui->create(p, api.c_str(), false)) return false;
        editor = true;
        gui->set_scale(p, 1.0);
        uint32_t cw = 0, ch = 0;
        gui->get_size(p, &cw, &ch);
        w = (int)cw;
        h = (int)ch;
        clap_window_t win{};
        win.api = api.c_str();
#if defined(_WIN32) || defined(__APPLE__)
        win.ptr = parent;
#else
        win.x11 = (clap_xwnd)(uintptr_t)parent;
#endif
        return gui->set_parent(p, &win) && gui->show(p);
    }
    void closeEditor() override {
        if (!editor) return;
        gui->hide(p);
        gui->destroy(p);
        editor = false;
    }
    void idle() override {
        if (callback.exchange(false)) p->on_main_thread(p);
    }
};

#endif

// ---- VST2 -----------------------------------------------------------------------------------------------------------
struct Vst2Plug : Plug {
    std::unique_ptr<Lib> lib;
    AEffect* fx = nullptr;
    VstTimeInfo time{};
    double rate = 48000;
    int block = 512;
    bool editor = false, active = false;
    std::vector<VstMidiEvent> midi;
    std::vector<char> list;

    static intptr_t VSTCALLBACK callback(AEffect* e, int32_t op, int32_t index, intptr_t value, void*, float) {
        Vst2Plug* me = e ? (Vst2Plug*)e->resvd1 : nullptr;
        switch (op) {
        case audioMasterVersion: return 2400;
        case audioMasterCurrentId: return e ? e->uniqueID : 0;
        case audioMasterGetTime: return me ? (intptr_t)&me->time : 0;
        case audioMasterSizeWindow:
            if (!me) return 0;
            me->wantW = index;
            me->wantH = (int)value;
            return 1;
        case audioMasterGetSampleRate: return me ? (intptr_t)me->rate : 48000;
        case audioMasterGetBlockSize: return me ? me->block : 512;
        case audioMasterGetCurrentProcessLevel: return onAudio ? 2 : 1;
        case audioMasterWantMidi: case audioMasterIdle: case audioMasterUpdateDisplay: case audioMasterIOChanged:
        case audioMasterAutomate: case audioMasterBeginEdit: case audioMasterEndEdit: return 1;
        case audioMasterCanDo: return 1;
        default: return 0;
        }
    }
    intptr_t call(int32_t op, int32_t index = 0, intptr_t value = 0, void* ptr = nullptr, float opt = 0) { return fx->dispatcher(fx, op, index, value, ptr, opt); }

    explicit Vst2Plug(const std::string& path) {
        lib = std::make_unique<Lib>(binaryOf(path));
        typedef AEffect* (VSTCALLBACK * Entry)(AudioMasterCallback);
        const Entry entry = (Entry)lib->sym("VSTPluginMain");
        if (!entry) return;
        fx = entry(callback);
        if (!fx || fx->magic != kEffectMagic) {
            fx = nullptr;
            return;
        }
        fx->resvd1 = (intptr_t)this;
        call(effOpen);
        char buf[256] = {};
        call(effGetEffectName, 0, 0, buf);
        name = buf;
    }
    ~Vst2Plug() override {
        if (!fx) return;
        if (editor) closeEditor();
        if (active) deactivate();
        call(effClose);
    }
    bool activate(double r, int b) override {
        rate = r;
        block = b;
        call(effSetSampleRate, 0, 0, nullptr, (float)r);
        call(effSetBlockSize, 0, b);
        call(effMainsChanged, 0, 1);
        call(effStartProcess);
        return active = true;
    }
    void deactivate() override {
        call(effStopProcess);
        call(effMainsChanged, 0, 0);
        active = false;
    }
    void process(float* l, float* r, int n, const std::vector<Note>& notes, bool playing, double tempo, double beats, int64_t pos) override {
        time = {};
        time.samplePos = (double)pos;
        time.sampleRate = rate;
        time.ppqPos = beats;
        time.tempo = tempo;
        time.timeSigNumerator = time.timeSigDenominator = 4;
        time.flags = kVstPpqPosValid | kVstTempoValid | kVstTimeSigValid | (playing ? kVstTransportPlaying : 0);
        if (!notes.empty()) {
            midi.assign(notes.size(), VstMidiEvent{});
            list.assign(sizeof(VstEvents) + notes.size() * sizeof(VstEvent*), 0);
            auto* ev = (VstEvents*)list.data();
            ev->numEvents = (int32_t)notes.size();
            for (size_t i = 0; i < notes.size(); ++i) {
                midi[i].type = kVstMidiType;
                midi[i].byteSize = sizeof(VstMidiEvent);
                std::memcpy(midi[i].midiData, notes[i].b, 3);
                ev->events[i] = (VstEvent*)&midi[i];
            }
            call(effProcessEvents, 0, 0, ev);
        }
        float* out[2] = {l, r};
        fx->processReplacing(fx, nullptr, out, n);
    }
    std::string save() override {
        void* data = nullptr;
        const intptr_t n = call(effGetChunk, 0, 0, &data);
        return n > 0 && data ? std::string((const char*)data, (size_t)n) : "";
    }
    bool load(const std::string& s) override {
        call(effSetChunk, 0, (intptr_t)s.size(), (void*)s.data());
        return true;
    }
    bool openEditor(void* parent, int& w, int& h) override {
        if (!(fx->flags & effFlagsHasEditor)) return false;
        ERect* rc = nullptr;
        call(effEditGetRect, 0, 0, &rc);
        call(effEditOpen, 0, 0, parent);
        editor = true;
        call(effEditGetRect, 0, 0, &rc);
        if (rc) {
            w = rc->right - rc->left;
            h = rc->bottom - rc->top;
        }
        return w > 0 && h > 0;
    }
    void closeEditor() override {
        if (!editor) return;
        call(effEditClose);
        editor = false;
    }
    void idle() override {
        if (editor) call(effEditIdle);
    }
};

// ---- VST3 -----------------------------------------------------------------------------------------------------------
#ifdef MT_VST3_HOST
using namespace Steinberg;
using namespace Steinberg::Vst;

// The editor's frame: its resizes come here, and on Linux it is the run loop the plug-in's timers and file
// descriptors are served by (the host's IRunLoop, which a VST3 editor on Linux has instead of an event loop)
struct RunLoop : Linux::IRunLoop {
    struct Timer { Linux::ITimerHandler* h; uint64 ms; Clock::time_point next; };
    std::vector<Timer> timers;
    std::vector<std::pair<Linux::IEventHandler*, int>> fds;
    tresult PLUGIN_API registerEventHandler(Linux::IEventHandler* h, Linux::FileDescriptor fd) override { fds.push_back({h, fd}); return kResultTrue; }
    tresult PLUGIN_API unregisterEventHandler(Linux::IEventHandler* h) override {
        fds.erase(std::remove_if(fds.begin(), fds.end(), [&](auto& f) { return f.first == h; }), fds.end());
        return kResultTrue;
    }
    tresult PLUGIN_API registerTimer(Linux::ITimerHandler* h, Linux::TimerInterval ms) override { timers.push_back({h, ms, Clock::now()}); return kResultTrue; }
    tresult PLUGIN_API unregisterTimer(Linux::ITimerHandler* h) override {
        timers.erase(std::remove_if(timers.begin(), timers.end(), [&](auto& t) { return t.h == h; }), timers.end());
        return kResultTrue;
    }
    void serve() {
        const auto now = Clock::now();
        for (size_t i = 0; i < timers.size(); ++i)
            if (now >= timers[i].next) {
                timers[i].next = now + std::chrono::milliseconds(timers[i].ms);
                timers[i].h->onTimer();
            }
#if !defined(_WIN32) && !defined(__APPLE__)
        for (size_t i = 0; i < fds.size(); ++i) {
            pollfd p{fds[i].second, POLLIN, 0};
            if (poll(&p, 1, 0) > 0) fds[i].first->onFDIsSet(fds[i].second);
        }
#endif
    }
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
#if SMTG_OS_LINUX   // the SDK defines its id on Linux alone, where a VST3 editor asks for it
        if (FUnknownPrivate::iidEqual(iid, Linux::IRunLoop::iid)) {
            *obj = this;
            return kResultOk;
        }
#endif
        (void)iid;
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
};

struct Frame : IPlugFrame {
    Plug* owner = nullptr;
    RunLoop* loop = nullptr;
    tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* r) override {
        owner->wantW = r->getWidth();
        owner->wantH = r->getHeight();
        view->onSize(r);
        return kResultTrue;
    }
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (FUnknownPrivate::iidEqual(iid, IPlugFrame::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = this;
            return kResultOk;
        }
        return loop->queryInterface(iid, obj);
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
};

struct Vst3Plug : Plug {
    VST3::Hosting::Module::Ptr module;
    IPtr<PlugProvider> provider;
    IPtr<IComponent> comp;
    IPtr<IEditController> ctl;
    FUnknownPtr<IAudioProcessor> audio;
    IPtr<IPlugView> view;
    HostProcessData data;
    EventList events;
    ProcessContext ctx{};
    RunLoop loop;
    Frame frame;
    double rate = 48000;
    bool active = false;
    static HostApplication& app() {
        static HostApplication a;
        return a;
    }

    explicit Vst3Plug(const std::string& path) {
        frame.owner = this;
        frame.loop = &loop;
        PluginContextFactory::instance().setPluginContext(&app());
        std::string err;
        module = VST3::Hosting::Module::create(path, err);
        if (!module) {
            std::printf("check_host: %s\n", err.c_str());
            return;
        }
        for (auto& ci : module->getFactory().classInfos())
            if (ci.category() == kVstAudioEffectClass) {
                provider = owned(new PlugProvider(module->getFactory(), ci, true));
                name = ci.name();
                break;
            }
        if (!provider || !provider->initialize()) {
            provider = nullptr;
            return;
        }
        comp = provider->getComponentPtr();
        ctl = provider->getControllerPtr();
        audio = FUnknownPtr<IAudioProcessor>(comp);
    }
    ~Vst3Plug() override {
        if (view) closeEditor();
        if (active) deactivate();
        audio = nullptr;
        ctl = nullptr;
        comp = nullptr;
        provider = nullptr;
    }
    bool ok() const { return comp && ctl && audio; }
    bool activate(double r, int block) override {
        rate = r;
        ProcessSetup setup{kRealtime, kSample32, block, r};
        if (audio->setupProcessing(setup) != kResultOk) return false;
        for (int32 i = 0; i < comp->getBusCount(kAudio, kOutput); ++i) comp->activateBus(kAudio, kOutput, i, i == 0);
        for (int32 i = 0; i < comp->getBusCount(kEvent, kInput); ++i) comp->activateBus(kEvent, kInput, i, true);
        if (comp->setActive(true) != kResultOk) return false;
        data.prepare(*comp, block, kSample32);
        return active = true;
    }
    void deactivate() override {
        comp->setActive(false);
        data.unprepare();
        active = false;
    }
    void startAudio() override { audio->setProcessing(true); }
    void stopAudio() override { audio->setProcessing(false); }
    void process(float* l, float* r, int n, const std::vector<Note>& notes, bool playing, double tempo, double beats, int64_t pos) override {
        events.clear();
        for (const Note& m : notes) {
            Event e{};
            e.busIndex = 0;
            e.sampleOffset = 0;
            const bool on = (m.b[0] & 0xf0) == 0x90 && m.b[2] > 0;
            e.type = on ? Event::kNoteOnEvent : Event::kNoteOffEvent;
            if (on) e.noteOn = {(int16)(m.b[0] & 15), (int16)m.b[1], 0.f, m.b[2] / 127.f, 0, -1};
            else e.noteOff = {(int16)(m.b[0] & 15), (int16)m.b[1], 0.f, -1, 0.f};
            events.addEvent(e);
        }
        ctx = {};
        ctx.state = ProcessContext::kTempoValid | ProcessContext::kProjectTimeMusicValid | ProcessContext::kTimeSigValid | (playing ? ProcessContext::kPlaying : 0);
        ctx.sampleRate = rate;
        ctx.projectTimeSamples = pos;
        ctx.projectTimeMusic = beats;
        ctx.tempo = tempo;
        ctx.timeSigNumerator = ctx.timeSigDenominator = 4;
        data.numSamples = n;
        data.inputEvents = &events;
        data.processContext = &ctx;
        audio->process(data);
        if (data.numOutputs > 0 && data.outputs[0].numChannels >= 2) {
            std::memcpy(l, data.outputs[0].channelBuffers32[0], sizeof(float) * n);
            std::memcpy(r, data.outputs[0].channelBuffers32[1], sizeof(float) * n);
        }
    }
    std::string save() override {
        MemoryStream s;
        if (comp->getState(&s) != kResultOk) return "";
        return std::string(s.getData(), (size_t)s.getSize());
    }
    bool load(const std::string& blob) override {
        MemoryStream s;
        int32 wrote = 0;
        s.write((void*)blob.data(), (int32)blob.size(), &wrote);
        s.seek(0, IBStream::kIBSeekSet, nullptr);
        if (comp->setState(&s) != kResultOk) return false;
        s.seek(0, IBStream::kIBSeekSet, nullptr);
        ctl->setComponentState(&s);
        return true;
    }
    bool openEditor(void* parent, int& w, int& h) override {
#ifdef _WIN32
        const FIDString type = kPlatformTypeHWND;
#elif defined(__APPLE__)
        const FIDString type = kPlatformTypeNSView;
#else
        const FIDString type = kPlatformTypeX11EmbedWindowID;
#endif
        view = owned(ctl->createView(ViewType::kEditor));
        if (!view || view->isPlatformTypeSupported(type) != kResultTrue) return false;
        view->setFrame(&frame);
        if (view->attached(parent, type) != kResultOk) return false;
        ViewRect r;
        view->getSize(&r);
        w = r.getWidth();
        h = r.getHeight();
        return true;
    }
    void closeEditor() override {
        if (!view) return;
        view->removed();
        view->setFrame(nullptr);
        view = nullptr;
    }
    void idle() override { loop.serve(); }
};
#endif

// ---- AU (macOS) -----------------------------------------------------------------------------------------------------
#ifdef __APPLE__
// The component registered in this process from its own bundle, with the factory its Info.plist names
// (AudioComponentRegister), so it opens without the system's AU registrar, which only a logged-in desktop session
// reaches; then hosted as an AU instrument is hosted: a stream format, the host's beat, tempo and transport through
// its callbacks, MIDI through MusicDeviceMIDIEvent, AudioUnitRender, the session through ClassInfo, and its Cocoa view.
static OSType fourcc(CFStringRef s) {
    char c[8] = {};
    if (!s || !CFStringGetCString(s, c, sizeof c, kCFStringEncodingASCII)) return 0;
    return (OSType)(uint8_t)c[0] << 24 | (OSType)(uint8_t)c[1] << 16 | (OSType)(uint8_t)c[2] << 8 | (uint8_t)c[3];
}

struct AuPlug : Plug {
    CFBundleRef bundle = nullptr;
    AudioComponentInstance au = nullptr;
    std::vector<char> lists;                // an AudioBufferList of two buffers
    double beat = 0, tempo = 125, sample = 0;   // what the host's callbacks answer, set before each render
    bool playing = false, wasPlaying = false;
    CFPropertyListRef classInfo = nullptr;  // the last ClassInfo, and the key in it of the plug-in's own bytes
    CFStringRef stateKey = nullptr;
    NSView* view = nil;
    NSSize shown{};

    explicit AuPlug(const std::string& path) {
        CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, (const UInt8*)path.c_str(), (CFIndex)path.size(), true);
        bundle = CFBundleCreate(nullptr, url);
        CFRelease(url);
        if (!bundle || !CFBundleLoadExecutable(bundle)) return;
        auto comps = (CFArrayRef)CFBundleGetValueForInfoDictionaryKey(bundle, CFSTR("AudioComponents"));
        if (!comps || CFGetTypeID(comps) != CFArrayGetTypeID() || !CFArrayGetCount(comps)) return;
        auto d = (CFDictionaryRef)CFArrayGetValueAtIndex(comps, 0);
        auto str = [&](CFStringRef k) { return (CFStringRef)CFDictionaryGetValue(d, k); };
        AudioComponentDescription desc{fourcc(str(CFSTR("type"))), fourcc(str(CFSTR("subtype"))), fourcc(str(CFSTR("manufacturer"))), 0, 0};
        auto factory = (AudioComponentFactoryFunction)CFBundleGetFunctionPointerForName(bundle, str(CFSTR("factoryFunction")));
        UInt32 version = 0;
        if (auto v = (CFNumberRef)CFDictionaryGetValue(d, CFSTR("version"))) CFNumberGetValue(v, kCFNumberSInt32Type, &version);
        if (!factory) return;
        static std::vector<std::pair<std::string, AudioComponent>> registered;   // once a process: a second instance finds it
        AudioComponent c = nullptr;
        for (auto& r : registered)
            if (r.first == path) c = r.second;
        if (!c) registered.push_back({path, c = AudioComponentRegister(&desc, str(CFSTR("name")), version, factory)});
        if (!c || AudioComponentInstanceNew(c, &au) != noErr) {
            au = nullptr;
            return;
        }
        char n[256] = {};
        CFStringGetCString(str(CFSTR("name")), n, sizeof n, kCFStringEncodingUTF8);
        name = n;
        if (name.find(": ") != std::string::npos) name = name.substr(name.find(": ") + 2);   // "vendor: name"
        lists.assign(offsetof(AudioBufferList, mBuffers) + 2 * sizeof(AudioBuffer), 0);
    }
    ~AuPlug() override {
        if (view) closeEditor();
        if (au) AudioComponentInstanceDispose(au);
        if (classInfo) CFRelease(classInfo);
    }
    static OSStatus beatAndTempo(void* me, Float64* b, Float64* t) {
        auto* p = (AuPlug*)me;
        if (b) *b = p->beat;
        if (t) *t = p->tempo;
        return noErr;
    }
    static OSStatus transport(void* me, Boolean* isPlaying, Boolean* isRecording, Boolean* changed, Float64* at, Boolean* cycling, Float64* c0, Float64* c1) {
        auto* p = (AuPlug*)me;
        if (isPlaying) *isPlaying = p->playing;
        if (isRecording) *isRecording = false;
        if (changed) *changed = p->playing != p->wasPlaying;
        if (at) *at = p->sample;
        if (cycling) *cycling = false;
        if (c0) *c0 = 0;
        if (c1) *c1 = 0;
        return noErr;
    }
    bool activate(double rate, int block) override {
        AudioStreamBasicDescription f{};
        f.mSampleRate = rate;
        f.mFormatID = kAudioFormatLinearPCM;
        f.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
        f.mBytesPerPacket = f.mBytesPerFrame = 4;
        f.mFramesPerPacket = 1;
        f.mChannelsPerFrame = 2;
        f.mBitsPerChannel = 32;
        if (AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f) != noErr) return false;
        UInt32 most = (UInt32)block;
        AudioUnitSetProperty(au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &most, sizeof most);
        HostCallbackInfo cb{};
        cb.hostUserData = this;
        cb.beatAndTempoProc = beatAndTempo;
        cb.transportStateProc2 = transport;
        AudioUnitSetProperty(au, kAudioUnitProperty_HostCallbacks, kAudioUnitScope_Global, 0, &cb, sizeof cb);
        return AudioUnitInitialize(au) == noErr;
    }
    void deactivate() override { AudioUnitUninitialize(au); }
    void process(float* l, float* r, int n, const std::vector<Note>& notes, bool pl, double tp, double bt, int64_t pos) override {
        wasPlaying = playing;
        beat = bt;
        tempo = tp;
        playing = pl;
        sample = (double)pos;
        for (const Note& m : notes) MusicDeviceMIDIEvent(au, m.b[0], m.b[1], m.b[2], 0);
        auto* list = (AudioBufferList*)lists.data();
        list->mNumberBuffers = 2;
        list->mBuffers[0] = {1, (UInt32)(n * 4), l};
        list->mBuffers[1] = {1, (UInt32)(n * 4), r};
        AudioTimeStamp ts{};
        ts.mSampleTime = (Float64)pos;
        ts.mFlags = kAudioTimeStampSampleTimeValid;
        AudioUnitRenderActionFlags flags = 0;
        if (AudioUnitRender(au, &flags, &ts, 0, (UInt32)n, list) != noErr) return;
        if (list->mBuffers[0].mData != l) std::memcpy(l, list->mBuffers[0].mData, sizeof(float) * n);   // the AU may hand its own buffers back
        if (list->mBuffers[1].mData != r) std::memcpy(r, list->mBuffers[1].mData, sizeof(float) * n);
    }
    // The plug-in's own bytes are the data value of its ClassInfo that holds Hollow's session; a load puts edited
    // bytes back under the same key of the last ClassInfo, as a host restoring a project restores it.
    std::string save() override {
        CFPropertyListRef pl = nullptr;
        UInt32 size = sizeof pl;
        if (AudioUnitGetProperty(au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &pl, &size) != noErr || !pl) return "";
        std::string out;
        CFStringRef key = nullptr;
        if (CFGetTypeID(pl) == CFDictionaryGetTypeID()) {
            const CFIndex n = CFDictionaryGetCount((CFDictionaryRef)pl);
            std::vector<const void*> keys((size_t)n), values((size_t)n);
            CFDictionaryGetKeysAndValues((CFDictionaryRef)pl, keys.data(), values.data());
            for (CFIndex i = 0; i < n; ++i)
                if (CFGetTypeID(values[(size_t)i]) == CFDataGetTypeID()) {
                    auto d = (CFDataRef)values[(size_t)i];
                    std::string bytes((const char*)CFDataGetBytePtr(d), (size_t)CFDataGetLength(d));
                    if (blobAt(bytes) != std::string::npos) {
                        out = bytes;
                        key = (CFStringRef)keys[(size_t)i];
                    }
                }
        }
        if (classInfo) CFRelease(classInfo);
        classInfo = pl;
        stateKey = key;
        return out;
    }
    bool load(const std::string& s) override {
        if (!stateKey) save();
        if (!stateKey) return false;
        CFMutableDictionaryRef d = CFDictionaryCreateMutableCopy(nullptr, 0, (CFDictionaryRef)classInfo);
        CFDataRef data = CFDataCreate(nullptr, (const UInt8*)s.data(), (CFIndex)s.size());
        CFDictionarySetValue(d, stateKey, data);
        CFRelease(data);
        CFPropertyListRef pl = d;
        const OSStatus st = AudioUnitSetProperty(au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &pl, sizeof pl);
        CFRelease(d);
        return st == noErr;
    }
    bool openEditor(void* parent, int& w, int& h) override {
        @autoreleasepool {
            UInt32 size = 0;
            Boolean writable = false;
            if (AudioUnitGetPropertyInfo(au, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, &size, &writable) != noErr || !size) return false;
            std::vector<char> buf(size);
            auto* info = (AudioUnitCocoaViewInfo*)buf.data();
            if (AudioUnitGetProperty(au, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, info, &size) != noErr) return false;
            NSBundle* b = [NSBundle bundleWithURL:(__bridge NSURL*)info->mCocoaAUViewBundleLocation];
            Class cls = [b classNamed:(__bridge NSString*)info->mCocoaAUViewClass[0]];
            CFRelease(info->mCocoaAUViewBundleLocation);
            CFRelease(info->mCocoaAUViewClass[0]);
            id<AUCocoaUIBase> factory = cls ? [[cls alloc] init] : nil;
            view = [factory uiViewForAudioUnit:au withSize:NSMakeSize(1315, 949)];
            if (!view) return false;
            [view retain];
            [(__bridge NSView*)parent addSubview:view];
            shown = [view frame].size;
            w = (int)shown.width;
            h = (int)shown.height;
            return w > 0 && h > 0;
        }
    }
    void closeEditor() override {
        if (!view) return;
        [view removeFromSuperview];
        [view release];
        view = nil;
    }
    void idle() override {   // an AU's view asks for a new size by taking it: its frame is the request
        if (!view) return;
        const NSSize s = [view frame].size;
        if (s.width != shown.width || s.height != shown.height) {
            shown = s;
            wantW = (int)s.width;
            wantH = (int)s.height;
        }
    }
};
#endif

// ---- the audio thread ---------------------------------------------------------------------------------------------
struct Feed {   // what the main thread hands the audio thread
    std::mutex m;
    std::vector<Note> notes;
    bool playing = false;
    double tempo = 125;
};

struct Audio {
    Plug& p;
    Feed& f;
    double rate;
    int block;
    std::atomic<bool> on{true};
    std::atomic<double> peak{0};
    std::atomic<bool> nan{false};
    std::atomic<long> blocks{0};
    std::thread th;
    Audio(Plug& p, Feed& f, double rate, int block) : p(p), f(f), rate(rate), block(block) { th = std::thread([this] { loop(); }); }
    ~Audio() {
        on = false;
        th.join();
    }
    void loop() {
        onAudio = true;
        std::vector<float> l((size_t)block), r((size_t)block);
        double beats = 0;
        int64_t pos = 0;
        p.startAudio();
        auto next = Clock::now();
        while (on) {
            std::vector<Note> notes;
            bool playing;
            double tempo;
            {
                std::lock_guard<std::mutex> g(f.m);
                notes.swap(f.notes);
                playing = f.playing;
                tempo = f.tempo;
            }
            if (!playing) beats = 0;   // a stopped transport sits at the start
            std::fill(l.begin(), l.end(), 0.f);
            std::fill(r.begin(), r.end(), 0.f);
            p.process(l.data(), r.data(), block, notes, playing, tempo, beats, pos);
            double pk = 0;
            for (int i = 0; i < block; ++i) {
                if (!std::isfinite(l[(size_t)i]) || !std::isfinite(r[(size_t)i])) nan = true;
                else pk = std::max(pk, (double)std::fabs(l[(size_t)i]) + std::fabs(r[(size_t)i]));
            }
            if (pk > peak) peak = pk;
            if (playing) beats += block / rate * tempo / 60;
            pos += block;
            ++blocks;
            next += std::chrono::nanoseconds((long long)(1e9 * block / rate));
            std::this_thread::sleep_until(next);
        }
        p.stopAudio();
    }
};

// ---- a window of the platform's own -----------------------------------------------------------------------------
struct HostWindow {
    int w = 0, h = 0;
    bool ok = false;
#ifdef _WIN32
    HWND hwnd = nullptr;
    HostWindow(int width, int height) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"check_host";
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        RegisterClassW(&wc);
        RECT r{0, 0, width, height};
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        hwnd = CreateWindowExW(0, L"check_host", L"check_host", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0, 0, r.right - r.left, r.bottom - r.top,
                               nullptr, nullptr, wc.hInstance, nullptr);
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        w = width;
        h = height;
        ok = hwnd != nullptr;
    }
    ~HostWindow() { DestroyWindow(hwnd); }
    void* handle() { return hwnd; }
    void resize(int width, int height) {
        RECT r{0, 0, width, height};
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        SetWindowPos(hwnd, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        w = width;
        h = height;
    }
    void pump() {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    HWND child() { return GetWindow(hwnd, GW_CHILD); }
    std::vector<uint32_t> capture() {
        std::vector<uint32_t> px((size_t)w * h);
        HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
        BITMAPINFO bi{};
        bi.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB};
        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HGDIOBJ old = SelectObject(dc, bmp);
        HWND c = child();
        PrintWindow(c ? c : hwnd, dc, 2 /* PW_RENDERFULLCONTENT */);
        std::memcpy(px.data(), bits, px.size() * 4);
        SelectObject(dc, old);
        DeleteObject(bmp);
        DeleteDC(dc);
        ReleaseDC(nullptr, screen);
        return px;
    }
    void click(int x, int y) {
        HWND c = child();
        const LPARAM at = MAKELPARAM(x, y);
        PostMessageW(c, WM_MOUSEMOVE, 0, at);
        PostMessageW(c, WM_LBUTTONDOWN, MK_LBUTTON, at);
        PostMessageW(c, WM_LBUTTONUP, 0, at);
    }
    // a letter key as the keyboard posts it: its virtual key and scan code, down and up
    void key(char letter) {
        HWND c = child();
        const UINT vk = (UINT)(letter - 'a' + 'A'), scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
        PostMessageW(c, WM_KEYDOWN, vk, 1 | scan << 16);
        PostMessageW(c, WM_KEYUP, vk, 1 | scan << 16 | 3u << 30);
    }
#elif defined(__APPLE__)
    NSWindow* win = nil;
    HostWindow(int width, int height) {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        win = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, width, height) styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
        [win setReleasedWhenClosed:NO];
        [win orderFrontRegardless];
        w = width;
        h = height;
        ok = win != nil;
    }
    ~HostWindow() { [win close]; }
    void* handle() { return (__bridge void*)[win contentView]; }
    void resize(int width, int height) {
        [win setContentSize:NSMakeSize(width, height)];
        w = width;
        h = height;
    }
    void pump() {
        for (NSEvent* e; (e = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate dateWithTimeIntervalSinceNow:0.002] inMode:NSDefaultRunLoopMode dequeue:YES]);)
            [NSApp sendEvent:e];
    }
    std::vector<uint32_t> capture() {
        NSView* v = [win contentView];
        [v displayIfNeeded];
        NSBitmapImageRep* rep = [v bitmapImageRepForCachingDisplayInRect:[v bounds]];
        [v cacheDisplayInRect:[v bounds] toBitmapImageRep:rep];
        std::vector<uint32_t> px((size_t)w * h);
        const int rw = (int)[rep pixelsWide], rh = (int)[rep pixelsHigh], spp = (int)[rep samplesPerPixel];
        const long stride = (long)[rep bytesPerRow];
        const unsigned char* b = [rep bitmapData];
        if (!b || [rep bitsPerSample] != 8 || spp < 3) return px;
        const int r0 = ([rep bitmapFormat] & NSBitmapFormatAlphaFirst) && spp == 4 ? 1 : 0;   // the colours after the alpha
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const unsigned char* q = b + (long)(y * rh / h) * stride + (long)(x * rw / w) * spp + r0;
                px[(size_t)y * w + x] = (uint32_t)q[0] << 16 | (uint32_t)q[1] << 8 | q[2];
            }
        return px;
    }
    void click(int x, int y) {
        const NSPoint p = NSMakePoint(x, h - y);
        for (NSEventType t : {NSEventTypeLeftMouseDown, NSEventTypeLeftMouseUp})
            [win sendEvent:[NSEvent mouseEventWithType:t location:p modifierFlags:0 timestamp:0 windowNumber:[win windowNumber] context:nil eventNumber:0 clickCount:1 pressure:1]];
    }
    void key(char letter) {   // the Mac's key code for the letter's place on a US keyboard (q, w and e only)
        const unsigned short code = letter == 'q' ? 12 : letter == 'w' ? 13 : 14;
        NSString* s = [NSString stringWithFormat:@"%c", letter];
        for (NSEventType t : {NSEventTypeKeyDown, NSEventTypeKeyUp})
            [win sendEvent:[NSEvent keyEventWithType:t location:NSZeroPoint modifierFlags:0 timestamp:0 windowNumber:[win windowNumber] context:nil
                                         characters:s charactersIgnoringModifiers:s isARepeat:NO keyCode:code]];
    }
#else
    Display* d = nullptr;
    ::Window xw = 0;
    HostWindow(int width, int height) {
        d = XOpenDisplay(nullptr);
        if (!d) return;
        const int s = DefaultScreen(d);
        xw = XCreateSimpleWindow(d, RootWindow(d, s), 0, 0, (unsigned)width, (unsigned)height, 0, BlackPixel(d, s), BlackPixel(d, s));
        XMapWindow(d, xw);
        XSync(d, False);
        w = width;
        h = height;
        ok = true;
    }
    ~HostWindow() {
        if (!d) return;
        XDestroyWindow(d, xw);
        XCloseDisplay(d);
    }
    void* handle() { return (void*)(uintptr_t)xw; }
    void resize(int width, int height) {
        XResizeWindow(d, xw, (unsigned)width, (unsigned)height);
        XSync(d, False);
        w = width;
        h = height;
    }
    void pump() {
        while (XPending(d)) {
            XEvent e;
            XNextEvent(d, &e);
        }
    }
    ::Window child() {
        ::Window root, parent, *kids = nullptr, c = 0;
        unsigned n = 0;
        if (XQueryTree(d, xw, &root, &parent, &kids, &n) && n) c = kids[n - 1];
        if (kids) XFree(kids);
        return c;
    }
    std::vector<uint32_t> capture() {
        XSync(d, False);
        std::vector<uint32_t> px((size_t)w * h);
        XImage* img = XGetImage(d, xw, 0, 0, (unsigned)w, (unsigned)h, AllPlanes, ZPixmap);
        if (!img) return px;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) px[(size_t)y * w + x] = (uint32_t)XGetPixel(img, x, y) & 0xffffff;
        XDestroyImage(img);
        return px;
    }
    void send(XEvent& e, long mask) {
        XSendEvent(d, e.xany.window, True, mask, &e);
        XSync(d, False);
    }
    void click(int x, int y) {
        XEvent e{};
        e.xbutton.display = d;
        e.xbutton.window = child();
        e.xbutton.root = DefaultRootWindow(d);
        e.xbutton.x = e.xbutton.x_root = x;
        e.xbutton.y = e.xbutton.y_root = y;
        e.xbutton.button = Button1;
        e.xbutton.same_screen = True;
        e.type = ButtonPress;
        send(e, ButtonPressMask);
        e.type = ButtonRelease;
        e.xbutton.state = Button1Mask;
        send(e, ButtonReleaseMask);
    }
    void key(char letter) {
        XEvent e{};
        e.xkey.display = d;
        e.xkey.window = child();
        e.xkey.root = DefaultRootWindow(d);
        e.xkey.keycode = XKeysymToKeycode(d, (KeySym)letter);
        e.xkey.same_screen = True;
        e.type = KeyPress;
        send(e, KeyPressMask);
        e.type = KeyRelease;
        send(e, KeyReleaseMask);
    }
#endif
};

// The main thread's own loop, for ms: the window's events and the plug-in's main-thread work
static void pump(Plug& p, HostWindow* w, int ms) {
    const auto end = Clock::now() + std::chrono::milliseconds(ms);
    do {
        if (w) w->pump();
        p.idle();
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    } while (Clock::now() < end);
}

// The loudest the plug-in played over the next ms
static double listen(Plug& p, Audio& a, HostWindow* w, int ms) {
    a.peak = 0;
    pump(p, w, ms);
    return a.peak.exchange(0);
}

static void play(Feed& f, const uint8_t (&m)[3]) {
    std::lock_guard<std::mutex> g(f.m);
    f.notes.push_back({{m[0], m[1], m[2]}});
}

static std::unique_ptr<Plug> openPlug(const std::string& format, const std::string& path) {
    std::unique_ptr<Plug> p;
#ifdef MT_CLAP_HOST
    if (format == "clap") {
        auto c = std::make_unique<ClapPlug>(path);
        if (c->p) p = std::move(c);
    }
#endif
    if (format == "vst2") {
        auto v = std::make_unique<Vst2Plug>(path);
        if (v->fx) p = std::move(v);
    }
#ifdef __APPLE__
    if (format == "au") {
        auto a = std::make_unique<AuPlug>(path);
        if (a->au) p = std::move(a);
    }
#endif
#ifdef MT_VST3_HOST
    if (format == "vst3") {
        auto v = std::make_unique<Vst3Plug>(path);
        if (v->ok()) p = std::move(v);
    }
#endif
    return p;
}

// ---- the editor, in a window ----------------------------------------------------------------------------------------
// Where things are, in the editor's own pixels at 100% (views/main.json, views/column.json): its size and the tracker's
// and the column's places, so a click lands on the same control at whatever size the editor came up.
static const int kW = 1315, kH = 949;
static int at(int v, int size, int base) { return (int)std::lround((double)v * size / base); }

static void editorChecks(const std::string& format, Plug& p, Audio& a, Feed& f) {
    HostWindow win(kW, kH);
    if (!win.ok) {
        say("SKIPPED the editor's checks: no display to open a window on");
        return;
    }
    say("editor");
    int w = 0, h = 0;
    const bool opened = p.openEditor(win.handle(), w, h);
    CHECK(opened && w > 0 && h > 0, "the editor did not open (%d x %d)", w, h);
    if (!opened) return;
    CHECK(std::abs(w * kH - h * kW) <= kW, "the editor's size %d x %d is not the window's shape", w, h);
    win.resize(w, h);
    pump(p, &win, 1500);
    auto px = win.capture();
    double lit;
    int colours;
    drawn(px, lit, colours);
    CHECK(lit > 0.15 && colours > 40, "the editor's window is not drawn (%.0f%% lit, %d colours)", lit * 100, colours);
    writePng(format + "_editor", px, w, h);

    // a click on the column's MIDI Trigger: the mode is in the session the format saves
    win.click(at(1052 + 20 + 100, w, kW), at(8 + 124 + 11, h, kH));
    pump(p, &win, 500);
    std::string s = p.save();
    CHECK(paramOf(s, "mode") == 2, "a click on MIDI Trigger in the editor's window did not choose it (mode %g)", paramOf(s, "mode"));
    writePng(format + "_editor_trigger", win.capture(), w, h);
    win.click(at(1052 + 20 + 100, w, kW), at(8 + 76 + 11, h, kH));   // and back to Free
    pump(p, &win, 300);

    // a click into the tracker's pattern editor, then a key: a note typed into the module, through the platform's keys
    const int before = xmNotes(unbase64(dataOf(p.save(), "module")));
    win.click(at(14 + 298, w, kW), at(14 + 560, h, kH));
    pump(p, &win, 300);
    win.key('q');
    pump(p, &win, 500);
    s = p.save();
    const int after = xmNotes(unbase64(dataOf(s, "module")));
    CHECK(after > before && before > 0, "a key typed into the tracker in the editor's window wrote no note (%d, then %d)", before, after);
    writePng(format + "_editor_typed", win.capture(), w, h);

    // 150%, as the column's size buttons set it: the plug-in asks its host for the bigger window
    p.wantW = p.wantH = 0;
    win.click(at(1052 + 144 + 22, w, kW), at(8 + 612 + 11, h, kH));
    pump(p, &win, 800);
    const int bw = p.wantW, bh = p.wantH;
    CHECK(std::abs(bw - w * 3 / 2) <= 2 && std::abs(bh - h * 3 / 2) <= 2, "150%% asked the host for %d x %d, not %d x %d", bw, bh, w * 3 / 2, h * 3 / 2);
    if (bw > 0 && bh > 0) {
        win.resize(bw, bh);
        pump(p, &win, 800);
        px = win.capture();
        drawn(px, lit, colours);
        CHECK(lit > 0.15, "at 150%% the window is not drawn");
        writePng(format + "_editor_150", px, bw, bh);
        win.click(at(1052 + 96 + 22, bw, kW), at(8 + 612 + 11, bh, kH));   // 100%
        pump(p, &win, 800);
        CHECK(std::abs(p.wantW - w) <= 2 && std::abs(p.wantH - h) <= 2, "100%% asked for %d x %d, not %d x %d", p.wantW.load(), p.wantH.load(), w, h);
        win.resize(w, h);
    }

    // closed and opened again, as a host does when its window is shut and brought back
    p.closeEditor();
    pump(p, &win, 300);
    int w2 = 0, h2 = 0;
    CHECK(p.openEditor(win.handle(), w2, h2) && w2 == w && h2 == h, "the editor did not open again at its size (%d x %d)", w2, h2);
    pump(p, &win, 1000);
    px = win.capture();
    drawn(px, lit, colours);
    CHECK(lit > 0.15, "the editor opened again is not drawn");
    // with the editor up, the song still plays through the host
    {
        std::lock_guard<std::mutex> g(f.m);
        f.playing = true;
    }
    p.load(withParam(p.save(), "mode", 1));
    pump(p, &win, 300);
    CHECK(listen(p, a, &win, 1200) > 0.01, "with the editor open, DAW Sync does not play");
    {
        std::lock_guard<std::mutex> g(f.m);
        f.playing = false;
    }
    pump(p, &win, 300);
    p.closeEditor();
    pump(p, &win, 200);
}

static int runPlugin(const std::string& format, const std::string& path, const std::string& modulePath) {
    std::ifstream mf(modulePath, std::ios::binary);
    const std::string xm((std::istreambuf_iterator<char>(mf)), std::istreambuf_iterator<char>());
    CHECK(xmNotes(xm) > 0, "%s is not an XM module", modulePath.c_str());

#if defined(_WIN32) && !defined(_WIN64)
    if (format == "vst2") {   // the DXi lives in the 32-bit VST2's DLL; registering it wants an elevated regsvr32, so its entry points are what is checked
        Lib dll(path);
        CHECK(dll.sym("DllGetClassObject") && dll.sym("DllRegisterServer") && dll.sym("DllUnregisterServer") && dll.sym("DllCanUnloadNow"),
              "the 32-bit DLL lacks the DXi's COM entry points");
    }
#endif
    say("load");
    auto p = openPlug(format, path);
    CHECK(p != nullptr, "%s did not load as %s", path.c_str(), format.c_str());
    if (!p) return 1;
    CHECK(p->name == "MilkyTracker.vst", "it calls itself \"%s\"", p->name.c_str());

    Feed f;
    CHECK(p->activate(48000, 512), "it did not activate at 48 kHz, 512");
    {
        Audio a(*p, f, 48000, 512);
        CHECK(listen(*p, a, nullptr, 600) == 0, "Free, with nothing playing, is not silent");

        say("session");
        const std::string fresh = p->save();
        CHECK(blobAt(fresh) != std::string::npos, "its saved session is not Hollow's (%zu bytes)", fresh.size());
        // the module put into the session as a DAW reloading a project puts it, in DAW Sync
        std::string s = withData(withData(fresh, "module.name", "milky.xm"), "module", base64(xm));
        CHECK(p->load(withParam(s, "mode", 1)), "it did not take a session back");
        pump(*p, nullptr, 800);
        {
            const std::string back = p->save();
            CHECK(paramOf(back, "mode") == 1 && dataOf(back, "module").size() > 1000, "the session came back without its mode (%g) or module (%zu)",
                  paramOf(back, "mode"), dataOf(back, "module").size());
        }
        {
            std::lock_guard<std::mutex> g(f.m);
            f.playing = true;
        }
        CHECK(listen(*p, a, nullptr, 1500) > 0.01, "DAW Sync: the session's song does not play with the host's transport");
        {
            std::lock_guard<std::mutex> g(f.m);
            f.playing = false;
        }
        pump(*p, nullptr, 300);
        CHECK(listen(*p, a, nullptr, 500) == 0, "DAW Sync: the song plays on after the transport stops");

        say("midi");
        p->load(withData(withParam(p->save(), "mode", 2), "trigger.seq.48", "00"));
        pump(*p, nullptr, 500);
        play(f, {0x90, 48, 100});
        CHECK(listen(*p, a, nullptr, 1000) > 0.01, "MIDI Trigger: a note on a pattern key from the host does not play its patterns");
        play(f, {0x80, 48, 0});
        pump(*p, nullptr, 300);
        CHECK(listen(*p, a, nullptr, 500) == 0, "MIDI Trigger: they play on after the note off");
        p->load(withParam(p->save(), "mode", 0));
        pump(*p, nullptr, 300);
        play(f, {0x90, 60, 100});
        CHECK(listen(*p, a, nullptr, 800) > 0.001, "Free: a note from the host does not play the instrument");
        play(f, {0x80, 60, 0});
        pump(*p, nullptr, 1500);

        const std::string saved = p->save();
        CHECK(dataOf(saved, "module").size() > 1000 && dataOf(saved, "trigger.seq.48") == "00",
              "the saved session lacks the module (%zu) or the key's patterns", dataOf(saved, "module").size());

        say("second instance");
        {
            auto q = openPlug(format, path);
            CHECK(q != nullptr, "a second instance did not load beside the first");
            if (q) {
                Feed g;
                CHECK(q->activate(44100, 256), "the second instance did not activate");
                Audio b(*q, g, 44100, 256);
                q->load(withParam(saved, "mode", 1));
                p->load(withParam(saved, "mode", 1));
                pump(*q, nullptr, 800);
                {
                    std::lock_guard<std::mutex> l1(f.m), l2(g.m);
                    f.playing = g.playing = true;
                }
                b.peak = 0;
                CHECK(listen(*p, a, nullptr, 1200) > 0.01 && b.peak.load() > 0.01, "two instances do not both play (%.3f, %.3f)", a.peak.load(), b.peak.load());
                {
                    std::lock_guard<std::mutex> l1(f.m), l2(g.m);
                    f.playing = g.playing = false;
                }
                pump(*q, nullptr, 300);
            }
        }
        {
            std::lock_guard<std::mutex> g(f.m);
            f.playing = true;
        }
        CHECK(listen(*p, a, nullptr, 1000) > 0.01, "the first instance stopped playing when the second closed");
        {
            std::lock_guard<std::mutex> g(f.m);
            f.playing = false;
        }
        pump(*p, nullptr, 300);
        p->load(withParam(saved, "mode", 0));
        pump(*p, nullptr, 300);

        editorChecks(format, *p, a, f);
        CHECK(!a.nan, "it played something that is not a number");
    }
    p->deactivate();

    say("rates and blocks");
    for (auto rb : {std::pair<double, int>{44100, 64}, {96000, 1024}, {48000, 333}}) {
        CHECK(p->activate(rb.first, rb.second), "it did not activate at %g, %d", rb.first, rb.second);
        {
            Audio a(*p, f, rb.first, rb.second);
            p->load(withParam(p->save(), "mode", 1));
            {
                std::lock_guard<std::mutex> g(f.m);
                f.playing = true;
            }
            CHECK(listen(*p, a, nullptr, 1000) > 0.01 && !a.nan, "at %g Hz, %d a block, DAW Sync does not play cleanly", rb.first, rb.second);
            {
                std::lock_guard<std::mutex> g(f.m);
                f.playing = false;
            }
            pump(*p, nullptr, 200);
        }
        p->deactivate();
    }
    p.reset();
    std::printf(fails ? "check_host %s: %d failed, %d passed\n" : "check_host %s: all %d passed\n", format.c_str(), fails ? fails : passes, passes);
    return fails ? 1 : 0;
}

// ---- the standalone app ---------------------------------------------------------------------------------------------
static int runStandalone(const std::string& app) {
    say("standalone");
#ifdef _WIN32
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    const std::wstring exe = fs::absolute(fs::u8path(app)).make_preferred().wstring();
    std::wstring cmd = L"\"" + exe + L"\"";
    CHECK(CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi), "%s did not start (error %lu)", app.c_str(), GetLastError());
    if (!pi.hProcess) return 1;
    HWND found = nullptr;
    for (auto end = Clock::now() + std::chrono::seconds(20); !found && Clock::now() < end;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        struct Find { DWORD pid; HWND w; } find{pi.dwProcessId, nullptr};
        EnumWindows([](HWND w, LPARAM l) -> BOOL {
            auto& f = *(Find*)l;
            DWORD pid = 0;
            GetWindowThreadProcessId(w, &pid);
            if (pid == f.pid && IsWindowVisible(w) && !GetWindow(w, GW_OWNER)) { f.w = w; return FALSE; }
            return TRUE;
        }, (LPARAM)&find);
        found = find.w;
    }
    CHECK(found != nullptr, "the standalone showed no window");
    if (found) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        wchar_t title[128] = {};
        GetWindowTextW(found, title, 128);
        CHECK(std::wstring(title) == L"MilkyTracker.vst", "the standalone's title bar reads \"%ls\"", title);
        RECT r;
        GetClientRect(found, &r);
        const int w = r.right, h = r.bottom;
        std::vector<uint32_t> px((size_t)w * h);
        HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
        BITMAPINFO bi{};
        bi.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB};
        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HGDIOBJ old = SelectObject(dc, bmp);
        PrintWindow(found, dc, 1 | 2 /* PW_CLIENTONLY | PW_RENDERFULLCONTENT */);
        std::memcpy(px.data(), bits, px.size() * 4);
        SelectObject(dc, old);
        DeleteObject(bmp);
        DeleteDC(dc);
        ReleaseDC(nullptr, screen);
        double lit;
        int colours;
        drawn(px, lit, colours);
        CHECK(lit > 0.15 && colours > 40, "the standalone's window is not drawn (%.0f%% lit, %d colours)", lit * 100, colours);
        writePng("standalone", px, w, h);
        PostMessageW(found, WM_CLOSE, 0, 0);
    }
    const DWORD waited = WaitForSingleObject(pi.hProcess, 15000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CHECK(waited == WAIT_OBJECT_0 && code == 0, "the standalone did not close cleanly (%s, exit %lu)", waited == WAIT_OBJECT_0 ? "closed" : "still running", code);
    if (waited != WAIT_OBJECT_0) TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
#elif defined(__APPLE__)
    (void)app;
    say("SKIPPED: the macOS standalone is checked by hand");
#else
    Display* d = XOpenDisplay(nullptr);
    if (!d) {
        say("SKIPPED the standalone: no display");
        return 0;
    }
    const ::Window root = DefaultRootWindow(d);
    auto tops = [&] {
        ::Window r, pa, *kids = nullptr;
        unsigned n = 0;
        std::vector<::Window> v;
        if (XQueryTree(d, root, &r, &pa, &kids, &n))
            for (unsigned i = 0; i < n; ++i) {
                XWindowAttributes wa;
                if (XGetWindowAttributes(d, kids[i], &wa) && wa.map_state == IsViewable && wa.width > 200) v.push_back(kids[i]);
            }
        if (kids) XFree(kids);
        return v;
    };
    const auto before = tops();
    const pid_t pid = fork();
    if (pid == 0) {
        execl(app.c_str(), app.c_str(), (char*)nullptr);
        _exit(127);
    }
    ::Window found = 0;
    for (auto end = Clock::now() + std::chrono::seconds(20); !found && Clock::now() < end;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        for (::Window w : tops())
            if (std::find(before.begin(), before.end(), w) == before.end()) found = w;
        int status;
        if (waitpid(pid, &status, WNOHANG) == pid) {
            CHECK(false, "the standalone exited at once (status %d)", status);
            XCloseDisplay(d);
            return 1;
        }
    }
    CHECK(found != 0, "the standalone showed no window");
    if (found) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        char* title = nullptr;
        XFetchName(d, found, &title);
        CHECK(title && std::string(title) == "MilkyTracker.vst", "the standalone's title bar reads \"%s\"", title ? title : "");
        if (title) XFree(title);
        XWindowAttributes wa;
        XGetWindowAttributes(d, found, &wa);
        std::vector<uint32_t> px((size_t)wa.width * wa.height);
        if (XImage* img = XGetImage(d, found, 0, 0, (unsigned)wa.width, (unsigned)wa.height, AllPlanes, ZPixmap)) {
            for (int y = 0; y < wa.height; ++y)
                for (int x = 0; x < wa.width; ++x) px[(size_t)y * wa.width + x] = (uint32_t)XGetPixel(img, x, y) & 0xffffff;
            XDestroyImage(img);
        }
        double lit;
        int colours;
        drawn(px, lit, colours);
        CHECK(lit > 0.15 && colours > 40, "the standalone's window is not drawn (%.0f%% lit, %d colours)", lit * 100, colours);
        writePng("standalone", px, wa.width, wa.height);
        // closed as a window manager closes it
        XEvent e{};
        e.xclient.type = ClientMessage;
        e.xclient.window = found;
        e.xclient.message_type = XInternAtom(d, "WM_PROTOCOLS", False);
        e.xclient.format = 32;
        e.xclient.data.l[0] = (long)XInternAtom(d, "WM_DELETE_WINDOW", False);
        e.xclient.data.l[1] = CurrentTime;
        XSendEvent(d, found, False, NoEventMask, &e);
        XFlush(d);
    }
    int status = -1;
    bool closed = false;
    for (auto end = Clock::now() + std::chrono::seconds(15); !closed && Clock::now() < end;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        closed = waitpid(pid, &status, WNOHANG) == pid;
    }
    CHECK(closed && WIFEXITED(status) && WEXITSTATUS(status) == 0, "the standalone did not close cleanly (%s, status %d)", closed ? "closed" : "still running", status);
    if (!closed) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
    }
    XCloseDisplay(d);
#endif
    std::printf(fails ? "check_host standalone: %d failed, %d passed\n" : "check_host standalone: all %d passed\n", fails ? fails : passes, passes);
    return fails ? 1 : 0;
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    mainThread = std::this_thread::get_id();
    const std::string mode = argc > 1 ? argv[1] : "";
    // the tracker writes its settings on the way out: into a folder of the check's own, not the user's
    const fs::path tmp = fs::temp_directory_path() / ("milkytracker_check_host_" + std::to_string(Clock::now().time_since_epoch().count()));
    fs::create_directories(tmp);
#ifdef _WIN32
    _putenv_s("APPDATA", tmp.string().c_str());
#else
    setenv("HOME", tmp.string().c_str(), 1);
#endif
    // a watchdog: a hang is a failure too
    std::thread([] {
        std::this_thread::sleep_for(std::chrono::minutes(4));
        std::printf("check_host: FAIL: still running after 4 minutes\n");
        std::fflush(stdout);
        std::_Exit(3);
    }).detach();
    int rc = 2;
    if (mode == "standalone" && argc >= 3) {
        if (argc > 3) shots = argv[3];
        rc = runStandalone(argv[2]);
    } else if ((mode == "clap" || mode == "vst2" || mode == "vst3" || mode == "au") && argc >= 4) {
#ifndef MT_VST3_HOST
        if (mode == "vst3") {
            say("SKIPPED: built without the VST3 SDK");
            return 0;
        }
#endif
#ifndef MT_CLAP_HOST
        if (mode == "clap") {
            say("SKIPPED: built without the CLAP headers");
            return 0;
        }
#endif
        if (argc > 4) shots = argv[4];
        rc = runPlugin(mode, argv[2], argv[3]);
    } else {
        std::printf("usage: check_host clap|vst3|vst2|au <plug-in> <module.xm> [<shots folder>]\n       check_host standalone <app> [<shots folder>]\n");
    }
    std::error_code ec;
    fs::remove_all(tmp, ec);
    return rc;
}
