// The tracker of one plug-in instance: MilkyTracker's own Tracker and screen, drawn into memory rather than a
// window, and the hollow::Pixels view through which the skin's "pixels" widget shows it and hands it the mouse and
// keyboard. Whatever touches the Tracker holds `lock`; the audio thread never does (plugin.cpp).
#pragma once
#include <hollow/hollow.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

class Tracker;
class PPScreen;
class PPDisplayDeviceHollow;
class PlayerController;
class MasterMixer;

class TrackerHost : public hollow::Pixels {
public:
    // The screen's size in its own pixels, and the host's audio until prepare() gives the real one.
    TrackerHost(int width, int height, int sampleRate, int bufferSize);
    ~TrackerHost() override;
    void shutDown();                  // saves the tracker's settings and closes it; the processor's destructor

    // hollow::Pixels, on the GUI thread
    int width() const override { return w_; }
    int height() const override { return h_; }
    bool frame(uint32_t* px, int dirty[4], bool whole) override;
    bool input(const hollow::PixelInput& e) override;
    void tick() override;

    // The processor's worker and main thread
    void setHostAudio(int sampleRate, int bufferSize);   // only while the host is not processing
    void note(int midiNote, int velocity);               // a MIDI note into the tracker: keyjazz and recording
    bool save(std::string& bytes, std::string& name);    // the current module as XM
    bool restore(const std::string& bytes, const std::string& name);
    std::string patternRows();                           // the module's patterns, a line each: number (hex), tab, rows
    // MilkyTracker's own Scale (its Config page's Layout tab, SCREENSCALEFACTOR in its settings): 1 or 2. The
    // skin's tracker widget shows the screen at it, a quarter at a time at 2 (plugin.cpp keeps the two in step).
    int scale();
    void setScale(int scale);

    // The audio thread's way to the player: the current tab's controller and the mixer it plays through, published
    // under the lock whenever the tracker may have switched tabs.
    std::atomic<PlayerController*> current{nullptr};
    MasterMixer* mixer = nullptr;

    std::recursive_mutex lock;

private:
    void publish();
    void raise(int id, const void* data, int size);
    void mouse(int id, int x, int y);
    void modifiers(const hollow::PixelInput& e);
    void key(const hollow::PixelInput& e);

    using Clock = std::chrono::steady_clock;
    int w_, h_;
    Tracker* tracker_ = nullptr;
    PPScreen* screen_ = nullptr;
    PPDisplayDeviceHollow* display_ = nullptr;
    bool left_ = false, right_ = false, double_ = false;
    int lastX_ = 0, lastY_ = 0, ticks_ = 0;
    Clock::time_point leftAt_, rightAt_, clickAt_;
    double wheel_ = 0;
    std::vector<uint16_t> held_;      // keys down, released for the tracker when the keyboard leaves
};
