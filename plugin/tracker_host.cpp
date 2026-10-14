// The tracker of one plug-in instance (tracker_host.h): MilkyTracker's Tracker on a screen in memory, fed the
// events its platform mains make (tracker/win32/Win32_main.cpp is the model for each one).
#include "tracker_host.h"
#include "Event.h"
#include "DisplayDeviceBase.h"
#include "Graphics.h"
#include "Screen.h"
#include "Tracker.h"
#include "PlayerMaster.h"
#include "PlayerController.h"
#include "ModuleEditor.h"
#include "PPSystem.h"
#include "TrackerSettingsDatabase.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>

// What every MilkyTracker platform main defines
pp_uint32 PPGetTickCount() {
    using namespace std::chrono;
    return (pp_uint32)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
#ifdef _WIN32
HWND hWnd = NULL;   // the owner of MilkyTracker's own Windows dialogs (osinterface/win32), set before each event
#endif

// PPUI calls this whenever it reads the modifiers. Windows asks the keyboard, as Win32_main does; elsewhere they
// stay what the last event said (TrackerHost::modifiers).
void QueryKeyModifiers() {
#ifdef _WIN32
    if (GetAsyncKeyState(VK_SHIFT) < 0) setKeyModifier(KeyModifierSHIFT); else clearKeyModifier(KeyModifierSHIFT);
    if (GetAsyncKeyState(VK_MENU) < 0) setKeyModifier(KeyModifierALT); else clearKeyModifier(KeyModifierALT);
    if (GetAsyncKeyState(VK_CONTROL) < 0) setKeyModifier(KeyModifierCTRL); else clearKeyModifier(KeyModifierCTRL);
#endif
}

// A screen in memory: PPUI draws 0x00RRGGBB into it, and what it updates is kept as one dirty rect.
class PPDisplayDeviceHollow : public PPDisplayDeviceBase {
public:
    std::vector<uint32_t> px;
    PPRect dirty;
    bool any = false;

    PPDisplayDeviceHollow(pp_int32 w, pp_int32 h) : PPDisplayDeviceBase(w, h, 1), px((size_t)w * h, 0) {
        auto* g = new PPGraphics_32bpp_generic(w, h, w * 4, px.data());
        g->setComponentBitpositions(16, 8, 0);
        g->lock = true;
        currentGraphics = g;
    }
    ~PPDisplayDeviceHollow() override { delete currentGraphics; }

    PPGraphicsAbstract* open() override {
        if (!isEnabled()) return NULL;
        currentGraphics->lock = false;
        return currentGraphics;
    }
    void close() override { currentGraphics->lock = true; }
    bool supportsScaling() const override { return true; }   // the Config page's Scale; the zoom is the skin's, so the device stays 1x
    void update() override { mark(PPRect(0, 0, size.width, size.height)); }
    void update(const PPRect& r) override { mark(r); }
    void setSize(const PPSize&) override {}   // the skin sets the size, not the tracker's settings
    void shutDown() override {}               // the host closes a plug-in, not the tracker's Exit

private:
    void mark(PPRect r) {
        if (!isUpdateAllowed() || !isEnabled()) return;
        r.x1 = std::max<pp_int32>(r.x1, 0);
        r.y1 = std::max<pp_int32>(r.y1, 0);
        r.x2 = std::min<pp_int32>(r.x2, size.width);
        r.y2 = std::min<pp_int32>(r.y2, size.height);
        if (r.x2 <= r.x1 || r.y2 <= r.y1) return;
        if (!any) {
            dirty = r;
            any = true;
            return;
        }
        dirty = PPRect(std::min(dirty.x1, r.x1), std::min(dirty.y1, r.y1), std::max(dirty.x2, r.x2), std::max(dirty.y2, r.y2));
    }
};

TrackerHost::TrackerHost(int width, int height, int sampleRate, int bufferSize) : w_(width), h_(height) {
    std::lock_guard<std::recursive_mutex> g(lock);
    tracker_ = new Tracker();
    display_ = new PPDisplayDeviceHollow(width, height);
    screen_ = new PPScreen(display_, tracker_);
    tracker_->setScreen(screen_);
    // before startUp applies the settings, so the mixer never opens at the settings' rate or block
    tracker_->getPlayerMaster()->setHostAudio(sampleRate, bufferSize);
    mixer = tracker_->getPlayerMaster()->getMixer();
    tracker_->startUp(true);
    publish();
}

TrackerHost::~TrackerHost() { shutDown(); }

void TrackerHost::shutDown() {
    std::lock_guard<std::recursive_mutex> g(lock);
    if (!tracker_) return;
    current = nullptr;
    tracker_->shutDown(false);
    PPEvent quit(eAppQuit);
    screen_->raiseEvent(&quit);
    delete tracker_;
    delete screen_;
    delete display_;
    tracker_ = nullptr;
    screen_ = nullptr;
    display_ = nullptr;
}

void TrackerHost::publish() { current = tracker_ ? tracker_->getPlayerController() : nullptr; }

int TrackerHost::scale() {
    std::lock_guard<std::recursive_mutex> g(lock);
    if (!tracker_) return 1;
    return tracker_->getSettingsDatabase()->restore("SCREENSCALEFACTOR")->getIntValue() >= 2 ? 2 : 1;
}

void TrackerHost::setScale(int scale) {
    std::lock_guard<std::recursive_mutex> g(lock);
    if (tracker_) tracker_->getSettingsDatabase()->store("SCREENSCALEFACTOR", scale >= 2 ? 2 : 1);
}

void TrackerHost::setHostAudio(int sampleRate, int bufferSize) {
    std::lock_guard<std::recursive_mutex> g(lock);
    if (tracker_) tracker_->getPlayerMaster()->setHostAudio(sampleRate, bufferSize);
}

bool TrackerHost::frame(uint32_t* px, int dirty[4], bool whole) {
    std::lock_guard<std::recursive_mutex> g(lock);
    if (!display_ || (!whole && !display_->any)) return false;
    PPRect r = whole ? PPRect(0, 0, w_, h_) : display_->dirty;
    for (pp_int32 y = r.y1; y < r.y2; ++y)
        std::copy_n(&display_->px[(size_t)y * w_ + r.x1], r.x2 - r.x1, px + (size_t)y * w_ + r.x1);
    dirty[0] = r.x1;
    dirty[1] = r.y1;
    dirty[2] = r.x2 - r.x1;
    dirty[3] = r.y2 - r.y1;
    if (!whole) display_->any = false;   // a whole copy is a snapshot, which leaves what changed for the next poll
    return true;
}

void TrackerHost::raise(int id, const void* data, int size) {
    if (!screen_) return;
    PPEvent e((EEventDescriptor)id, (void*)data, size);
    screen_->raiseEvent(&e);
}

void TrackerHost::mouse(int id, int x, int y) {
    PPPoint p(x, y);
    raise(id, &p, sizeof p);
}

// Where QueryKeyModifiers cannot ask the keyboard, the modifiers are what the event says.
void TrackerHost::modifiers(const hollow::PixelInput& e) {
#ifndef _WIN32
    if (e.shift) setKeyModifier(KeyModifierSHIFT); else clearKeyModifier(KeyModifierSHIFT);
    if (e.alt) setKeyModifier(KeyModifierALT); else clearKeyModifier(KeyModifierALT);
    if (e.ctrl) setKeyModifier(KeyModifierCTRL); else clearKeyModifier(KeyModifierCTRL);
#else
    (void)e;
#endif
}

// Win32_main's key events: { virtual key, scan code, the key's unshifted character }, the numeric pad's keys
// told apart by scan code whatever Num Lock says (its EnableNumPad), right Alt and right Ctrl as keys of their
// own, and a key's typed character after it as eKeyChar.
void TrackerHost::key(const hollow::PixelInput& e) {
    const bool ext = (e.scan & 0x100) != 0;
    pp_uint16 vk = (pp_uint16)e.vk;
    if (vk == VK_MENU && ext) vk = VK_RMENU;
    else if (vk == VK_CONTROL && ext) vk = VK_RCONTROL;
#ifdef __APPLE__
    // as MilkyTracker's Cocoa port has them: right Command (Hollow's right Ctrl) plays the pattern, right Option the song
    if (vk == VK_RCONTROL) vk = VK_RMENU;
    else if (vk == VK_RMENU) vk = VK_RCONTROL;
#endif
    unsigned plain = e.plain >= 'A' && e.plain <= 'Z' ? e.plain + ('a' - 'A') : e.plain;
    pp_uint16 chr[3] = {vk, (pp_uint16)(e.scan & 0xff), (pp_uint16)(plain < 128 ? plain : 0)};
    switch (e.scan & 0xff) {
    case 0x35: if (ext) chr[0] = VK_DIVIDE; break;
    case 0x37: if (ext) chr[0] = VK_MULTIPLY; break;
    case 0x4A: if (ext) chr[0] = VK_SUBTRACT; break;
    case 0x4E: if (ext) chr[0] = VK_ADD; break;
    case 0x1C: if (ext) chr[0] = VK_SEPARATOR; break;
    case 0x53: if (!ext) chr[0] = VK_DECIMAL; break;
    case 0x52: if (!ext) chr[0] = VK_NUMPAD0; break;
    case 0x4F: if (!ext) chr[0] = VK_NUMPAD1; break;
    case 0x50: if (!ext) chr[0] = VK_NUMPAD2; break;
    case 0x51: if (!ext) chr[0] = VK_NUMPAD3; break;
    case 0x4B: if (!ext) chr[0] = VK_NUMPAD4; break;
    case 0x4C: if (!ext) chr[0] = VK_NUMPAD5; break;
    case 0x4D: if (!ext) chr[0] = VK_NUMPAD6; break;
    case 0x47: if (!ext) chr[0] = VK_NUMPAD7; break;
    case 0x48: if (!ext) chr[0] = VK_NUMPAD8; break;
    case 0x49: if (!ext) chr[0] = VK_NUMPAD9; break;
    }
    if (vk == VK_CAPITAL) chr[1] = 0x100;
#ifdef _WIN32
    // Windows reads the keyboard itself for modifiers (QueryKeyModifiers), which holds what the event says for a key
    // typed; a key a host or a check hands over has them only in the event, so Ctrl and Alt are forced for its length
    // (Shift is the tracker's own on-screen keyboard's to force).
    struct Forced {
        pp_uint32 bits = 0;
        explicit Forced(const hollow::PixelInput& e) {
            if (e.ctrl) bits |= KeyModifierCTRL;
            if (e.alt) bits |= KeyModifierALT;
            if (bits) setForceKeyModifier((KeyModifiers)bits);
        }
        ~Forced() { if (bits) clearForceKeyModifier((KeyModifiers)bits); }
    } forced(e);
#endif
    if (e.type == hollow::PixelInput::KeyUp) {
        held_.erase(std::remove(held_.begin(), held_.end(), chr[0]), held_.end());
        raise(eKeyUp, chr, sizeof chr);
        return;
    }
    if (std::find(held_.begin(), held_.end(), chr[0]) == held_.end()) held_.push_back(chr[0]);
    raise(eKeyDown, chr, sizeof chr);
    // a WM_CHAR comes only for what a key types, and an Alt chord's is a WM_SYSCHAR, which Win32_main ignores
    if (e.ch >= 32 && e.ch <= 127 && !(e.alt && !e.ctrl)) {
        pp_uint16 c = (pp_uint16)e.ch;
        raise(eKeyChar, &c, sizeof c);
    }
}

bool TrackerHost::input(const hollow::PixelInput& e) {
    using In = hollow::PixelInput;
    std::lock_guard<std::recursive_mutex> g(lock);
    if (!tracker_) return false;
#ifdef _WIN32
    hWnd = GetActiveWindow();
#endif
    modifiers(e);
    const Clock::time_point now = Clock::now();
    switch (e.type) {
    case In::Down:
        // a second press soon after and close to the first makes the release a double-click, as in Win32_main
        double_ = now - clickAt_ < std::chrono::milliseconds(500) && std::abs(e.x - lastX_) < 4 && std::abs(e.y - lastY_) < 4;
        clickAt_ = double_ ? Clock::time_point() : now;
        left_ = true;
        leftAt_ = now;
        lastX_ = e.x;
        lastY_ = e.y;
        mouse(eLMouseDown, e.x, e.y);
        break;
    case In::Drag:
        lastX_ = e.x;
        lastY_ = e.y;
        mouse(eLMouseDrag, e.x, e.y);
        break;
    case In::Up:
        left_ = false;
        if (double_) mouse(eLMouseDoubleClick, e.x, e.y);
        double_ = false;
        mouse(eLMouseUp, e.x, e.y);
        break;
    case In::Move:
        lastX_ = e.x;
        lastY_ = e.y;
        mouse(eMouseMoved, e.x, e.y);
        break;
    case In::RightDown:
        right_ = true;
        rightAt_ = now;
        lastX_ = e.x;
        lastY_ = e.y;
        mouse(eRMouseDown, e.x, e.y);
        break;
    case In::RightDrag:
        lastX_ = e.x;
        lastY_ = e.y;
        mouse(eRMouseDrag, e.x, e.y);
        break;
    case In::RightUp:
        right_ = false;
        mouse(eRMouseUp, e.x, e.y);
        break;
    case In::Wheel: {   // three lines a notch, as Windows sets it by default
        wheel_ += e.wheel * 3;
        TMouseWheelEventParams w;
        w.pos = PPPoint(e.x, e.y);
        w.deltaX = 0;
        w.deltaY = (pp_int32)wheel_;
        wheel_ -= w.deltaY;
        if (w.deltaY) raise(eMouseWheelMoved, &w, sizeof w);
        break;
    }
    case In::KeyDown:
    case In::KeyUp:
        key(e);
        break;
    case In::Char:
        if (e.ch >= 32 && e.ch <= 127) {
            pp_uint16 c = (pp_uint16)e.ch;
            raise(eKeyChar, &c, sizeof c);
        }
        break;
    case In::Focus:
        break;
    case In::Blur:   // the keyboard went elsewhere: no key is held here any more, nor a button
        for (uint16_t k : std::vector<uint16_t>(held_)) {
            pp_uint16 chr[3] = {k, 0, 0};
            raise(eKeyUp, chr, sizeof chr);
        }
        held_.clear();
        clearKeyModifier(KeyModifierSHIFT);
        clearKeyModifier(KeyModifierALT);
        clearKeyModifier(KeyModifierCTRL);
        if (left_) mouse(eLMouseUp, -1000, -1000);
        if (right_) mouse(eRMouseUp, -1000, -1000);
        left_ = right_ = double_ = false;
        break;
    }
    publish();
    return e.type == In::KeyDown || e.type == In::KeyUp || e.type == In::Char;
}

// Win32_main's timer: eTimer, and a held button repeats after half a second (the left every third 20 ms tick)
void TrackerHost::tick() {
    std::lock_guard<std::recursive_mutex> g(lock);
    if (!tracker_) return;
#ifdef _WIN32
    hWnd = GetActiveWindow();
#endif
    raise(eTimer, nullptr, 0);
    ++ticks_;
    const Clock::time_point now = Clock::now();
    if (left_ && now - leftAt_ > std::chrono::milliseconds(500) && !(ticks_ % 2)) mouse(eLMouseRepeat, lastX_, lastY_);
    else if (right_ && now - rightAt_ > std::chrono::milliseconds(500)) mouse(eRMouseRepeat, lastX_, lastY_);
    publish();
}

// As MidiReceiver_win32 does: one octave down, the instrument's own volume.
void TrackerHost::note(int midiNote, int velocity) {
    std::lock_guard<std::recursive_mutex> g(lock);
    if (!tracker_) return;
    const int n = std::max(midiNote - 12, 1);
    if (velocity > 0) tracker_->sendNoteDown(n, -1);
    else tracker_->sendNoteUp(n);
    publish();
}

std::string TrackerHost::patternRows() {
    std::lock_guard<std::recursive_mutex> g(lock);
    std::string rows;
    XModule* m = tracker_ ? tracker_->getModuleEditor()->getModule() : nullptr;
    for (int i = 0; m && i < m->header.patnum; ++i) {
        char b[32];
        std::snprintf(b, sizeof b, "%02X\t%d rows\n", i, m->phead[i].rows);
        rows += b;
    }
    return rows;
}

static std::string tempFile() {
    const SYSCHAR* t = System::getTempFileName();
    return std::string(t ? t : "milkytracker_plugin.tmp");
}

bool TrackerHost::save(std::string& bytes, std::string& name) {
    std::lock_guard<std::recursive_mutex> g(lock);
    if (!tracker_) return false;
    const std::string path = tempFile();
    bool ok = tracker_->saveModule(PPSystemString(path.c_str())) == 0;
    if (ok) {
        std::ifstream f(path, std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        ok = !bytes.empty();
        PPSystemString n = tracker_->getModuleEditor()->getModuleFileName();
        name = n.getStrBuffer();
    }
    std::remove(path.c_str());
    return ok;
}

bool TrackerHost::restore(const std::string& bytes, const std::string& name) {
    std::lock_guard<std::recursive_mutex> g(lock);
    if (!tracker_) return false;
    const std::string path = tempFile();
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        f.write(bytes.data(), (std::streamsize)bytes.size());
    }
    const bool ok = tracker_->restoreModule(PPSystemString(path.c_str()), PPSystemString(name.c_str()));
    std::remove(path.c_str());
    publish();
    return ok;
}
