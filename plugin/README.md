# MilkyTracker, the plug-in

MilkyTracker running inside a DAW as an instrument plug-in: the whole tracker, as it is, on the left of the window, a column of plug-in controls on the right and a keyboard along the bottom. It is built on [Hollow](../hollow/docs/framework.md), the framework FSVR uses, so it comes in every format FSVR does: CLAP, VST3, VST2 and the standalone on Windows, with the 32-bit VST2 and its DXi; CLAP, VST3, VST2, AU and the standalone on macOS, as universal binaries; CLAP, VST3, VST2 and the standalone on Linux, and its art is FSVR's Blender kit rendered in dark violet chrome to match MilkyTracker's logo, with the column's text in MilkyTracker's own bitmap fonts (its System 8x8 and Large 12x12 faces) and the title in Analog Whispers beside MilkyTracker's milk carton.

## Building

From the repository's root, with CMake 3.22+ and, on Windows, Visual Studio 2022 ("Desktop development with C++"). The first configure downloads the plug-in SDKs.

```bash
cmake -S . -B build/plugin -G "Visual Studio 17 2022" -A x64 -DMILKYTRACKER_PLUGIN=ON
```

```bash
cmake --build build/plugin --config Release
```

The same with `-A Win32` into `build/plugin-x86` builds the 32-bit VST2 with the DXi in it. On macOS and Linux it is `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DMILKYTRACKER_PLUGIN=ON`, then `cmake --build build`. Linux needs `libx11-dev`, and for the standalone's audio and MIDI `libasound2-dev`, `libjack-jackd2-dev` and `libpulse-dev`; the plug-in itself needs no SDL. The macOS standalone app needs Xcode (clap-wrapper compiles its menu with `ibtool`); with the command line tools alone everything else builds and the app is left out with a warning.

Every format lands in `bin/<format>/` as `MilkyTrackerVST` (`MilkyTrackerVST.clap`, `.vst3`, `.dll` or `.so`, `.component`, `MilkyTrackerVST.exe`); hosts list it, and the standalone's title bar shows it, as MilkyTracker.vst. Its ids are those it always had, so projects saved with it still find it. Without `MILKYTRACKER_PLUGIN` the root CMakeLists.txt builds the tracker as upstream does. The plug-in keeps its own settings file (`milkytracker_plugin.cfg` beside the standalone's `milkytracker.cfg`), so its audio, window and driver settings never touch the standalone's.

## Testing

```bash
python plugin/test.py
```

That builds every format and runs every check on all three platforms: Windows here (x64 and the 32-bit build), Linux on the `legion` laptop and macOS on the Mac VM it runs, both over ssh as `../VirtualMac/README.txt` sets them up. Name platforms to run only those (`python plugin/test.py windows linux`), and `--no-build` runs the checks on what is built. The source goes to each machine as a tar that keeps its files' times, so a rebuild there is incremental. Screenshots and logs come back to `build/test-report/<platform>/`, and `build/test-report/summary.md` lists what passed. On one machine, `ctest --test-dir build/plugin -C Release` runs the same checks after a build.

| Check | What it does |
|---|---|
| `plugin` (`check_plugin.cpp`) | The processor as a host drives it: a session's module restored, the screen drawn, DAW Sync started and stopped by the transport, again after the host prepares at another rate and when a session loads mid-playback, MIDI Trigger's keys and transpose, the dialog's commands, the strip's drags, the module saved back |
| `gui` (`check_gui.cpp`) | The editor without a window, over the real skin and processor, driven as a mouse and a keyboard drive it: every column control, both keyboards and their neon zones (by their pixels), the strip, the patterns dialog's lists and buttons, notes played from the keys, the tracker clicked into and typed into (the note read back out of the saved module), played and stopped from its own keys, scrolled and right-clicked, every scale, the tracker at 2x (its quarters checked against its screen, panned by its bars, clicked and typed into, and set from MilkyTracker's own Config page), the editor closed and opened again, every visible control clicked once, and the session restored into a new instance, at the tracker's own scale rather than the session's. `MT_GUI_SHOTS=<folder>` saves a picture at each step |
| `host_clap`, `host_vst3`, `host_vst2`, `host_au` (`check_host.cpp`) | The built binary loaded as a host loads it (the AU registered in the check's own process from its bundle, so it needs no system registrar), with an audio thread running it in real time: the module put in through the format's own state, DAW Sync on the host's transport, MIDI Trigger and Free on the host's notes, a second instance beside the first, 44.1, 48 and 96 kHz in blocks of 64, 333 and 1024, and the editor in a real window (Win32, X11 or Cocoa): captured, clicked (a mode chosen), typed into (a note entered in the tracker), resized to 150% through the format's own resize request, closed and opened again. A hang fails it too |
| `host_standalone` (`check_host.cpp`) | The standalone app started, its window captured, and closed as a user closes it, which must end it cleanly |
| `vst3_validator` | Steinberg's VST3 validator, built from the SDK clap-wrapper fetches: its 47 conformance tests |
| `auval` (macOS, from `test.py`) | Apple's AU validation of the installed component. It runs only while someone is logged in to the Mac's desktop, where the AU registrar lists third-party AUs; otherwise the summary says it was skipped |

Linux runs the checks under Xvfb. On macOS they run in the logged-in desktop session when there is one (a `.command` file opened in the background, as FM8.plus's `tools/mac/ingui.sh` does); with no one logged in they run over ssh, where there is no window server, and the editor runs in an offscreen Cocoa window: the view, its drawing and its event handling are the real ones, and its picture is the view's own. `check_host` and `check_gui` keep the tracker's settings in a folder of their own, not the user's.

## The three modes

The Mode group at the top of the right column picks one.

- **Free.** The tracker plays as itself, at its own BPM, started and stopped from its own buttons and keys. MIDI notes from the host, and the on-screen keyboard, play and record through the current instrument exactly as MilkyTracker's own MIDI input does (one octave down, so MIDI middle C is the tracker's C-4).
- **DAW Sync.** The tracker's BPM follows the host's, times the Ratio: at x1 the tracker's BPM is the host's, which at speed 6 is four rows a beat, as Fasttracker II counts it; x2 is eight rows a beat at speed 6, x3/2 six, x1/2 two, and so on. Speed changes in the song still change how long a row lasts, so swing written with alternating speeds survives. The song starts and stops with the host's transport, at the order and row the host's position falls on (counted along the order list at the song's starting speed, round again past its end), and goes there again whenever the host jumps, on a loop or a click in the timeline, and when a session loads while the host plays. Between jumps the tempo alone keeps the two together, so the song's own `Bxx` and `Dxx` jumps play as written. MIDI notes play through the tracker as in Free.
- **MIDI Trigger.** The keyboard has two zones that light up in neon: the **green** pitch keys and the **blue** pattern keys. Drag them on the strip under the keys: a zone's middle moves it, its ends stretch it, and its marker moves the Home key (green) or picks the key being edited (blue); the column's numbers set the same things exactly. A pattern key plays the patterns listed for it, one after another. Right-click a blue key, or press the column's Plays button, to open its patterns: the song's patterns on the left, the key's list on the right, with Add, Remove, Up, Down and Clear. A pitch key moves the pitch of whatever plays by its distance from Home, and the pitch holds until another pitch key moves it; Transpose shows and sets it. Gate plays a key's patterns while it is held and Latch starts them on one press and stops them on the next; with Loop the list starts over after its last pattern. Notes outside both zones play through the tracker as in Free. When the host reports a tempo, the patterns play at it, times the Ratio.

## How it is put together

| File | What it is |
|---|---|
| `plugin.cpp` | The processor: pulls the tracker's mix, the three modes, the worker thread, the session |
| `tracker_host.cpp` | MilkyTracker's Tracker on a screen in memory, and the view that shows it in the skin |
| `skin/` | The Hollow skin: `views/main.json` (the tracker, the column, the two keyboards and the zone strip), `views/column.json`, `views/dialog_patterns.json`, `params.json` |
| `../blender/` | The art: FSVR's Blender kit, recoloured violet (`scripts/violet_textures.py`, `scripts/kit.py`'s `violet()`) and darkened (`kit.SHADE`), with this window's layout in `scripts/layout.py`; `scripts/mt_fonts.py` makes MilkyTracker's fonts into glyph strips, `scripts/aw_font.py` the title's and the wordmark's Analog Whispers, `scripts/carton.py` the carton |
| `../hollow/` | Hollow's framework, with the additions this plug-in needed (below) |

- **Greyed out.** What a mode leaves unused is greyed out with its label (`enableIf` in `views/column.json`, the skin's `"disabled"` in `skin.json`): the Ratio and the sync screen in Free, MIDI Trigger's controls outside MIDI Trigger.
- **The screen.** The Tracker draws into a 1024 x 720 buffer (`PPDisplayDeviceHollow`), which the skin's `pixels` widget shows pixel for pixel at 100% and scales by nearest pixel at 150% and 200%.
- **Tracker 1x and 2x.** MilkyTracker's own Scale, on its Config page's Layout tab, offers x1 and x2 in the plug-in, and the column's Tracker buttons under Window size set the same thing. At 2x the window and the tracker's area keep their size and show a quarter of the tracker at a time, each pixel doubled; the two neon bars on the well's dividers, beside and below it, pan it by a drag, a click or the wheel, and clicks and keys land where they show. It applies at once, with no restart. It is the user's setting, kept in the tracker's own settings file (`SCREENSCALEFACTOR`), not the session's: a project opens at whatever the tracker was last set to. The processor's worker keeps the param and the setting in step (`syncScale`). Mouse and keyboard events reach it as MilkyTracker's Windows port makes them (`src/tracker/win32/Win32_main.cpp`): the same virtual keys and scan codes, double-clicks on the second release, held buttons repeating after half a second. Click into the tracker and it has the keyboard, ahead of the host's shortcuts, until the window loses focus.
- **The sound.** MilkyPlay mixes in 250 Hz beat packets (192 samples at 48 kHz). The processor asks the mixer for one packet at a time and plays it out, and applies MIDI between packets, which is the player's own tick grain. The mix is MilkyTracker's own 16-bit output. The rate and block always come from the host; the settings page's choices of those are ignored.
- **Threads.** The Tracker is only ever touched under its lock, by the GUI thread or by the processor's worker, which takes MIDI notes into the tracker, restores a session's module and keeps the pattern lists. The audio thread never takes that lock. It starts, stops and transposes the current tab's player directly between mixes (`PlayerController::host*`), which is safe because the mixing thread is the one calling, and it checks the mixer still holds that tab's player, since a closing tab's player is freed only after the mixer lets go of it.
- **Sessions.** The module goes into the host's session as an XM file, and comes back out of it on load without asking to save the one it replaces. The column's settings and each key's pattern list are saved with the session too.
- **The patterns dialog** is a Hollow modal over two lists. Its buttons leave a command in text data (`live.patterns.cmd`) that the worker carries out on the picked key's list and clears.

## What changed outside `plugin/`

- **The build** (`CMakeLists.txt`, `src/*/CMakeLists.txt`): the plug-in on Linux needs no SDL, ALSA or JACK, since it draws into memory and plays through the host, and its dialogs are `src/ppui/osinterface/plugin/PPDialogs_LINUX.cpp` (zenity's, else kdialog's, as child processes, since the SDL build's run an event loop of their own). The universal macOS build leaves out the autotools-built LHA and ZIP unpackers. `NOMINMAX` is the tracker's own, so the DXi's DirectShow base classes keep windows.h's `min` and `max`.

- **MilkyPlay** (`src/milkyplay`): `PlayerSTD::setTickRate` (ticks a second from outside, worked out from the beat packet's real length, since at 44.1 kHz a packet is 176 samples, not 176.4, and the song's own BPM runs 0.23% fast there), `setTranspose`, `setPatternQueue` (pattern play through a list, advancing at a pattern's end and at a `Dxx` or `Bxx` in it), `MasterMixer::isDeviceAlive`, and a driver list of the NULL driver alone when built as a plug-in.
- **The tracker** (`src/tracker`): the Config page's Scale offers x1 and x2 alone in the plug-in and needs no restart for it, and the plug-in's default is x1 (`SectionSettings.cpp`, `TrackerSettings.cpp`); `PlayerController`'s `host*` calls, `PlayerMaster::setHostAudio`, `Tracker::restoreModule`, `shutDown(false)` (no questions, for a plug-in closing), and accessors for the player.
- **Hollow** (`hollow/`): the `pixels` custom kind and `hollow::Pixels` (a GUI the product draws), raw keys for a widget that takes the keyboard whole (Windows), `Processor::transport` (CLAP and VST2), the piano's neon `zones` and `marks` and its right-click `pick`, the `key_zones` strip that drags them, the `pixels` kind's `zoom` and pan, and the `pan` scrollbars that move it. Raw keys now come on macOS (`mac.mm`: key down and up, modifier keys alone, Command chords) and Linux (`linux.cpp`: key releases, detectable auto-repeat) as Windows virtual keys and scan codes, so the tracker types the same everywhere. `HollowClapWrapperFix.cmake` makes clap-wrapper's AU report beat 0 as a position (it sent a host starting from the song's start without one, and DAW Sync never started). `HollowPlugin.cmake` pulls the skin table in by name on Linux, where GNU ld reads static libraries once (FSVR only linked because its own code reads the table), and leaves the macOS app out when there is no `ibtool`. `hollow/docs` describes each; they belong in Hollow's own `framework/` too.

## Not done yet

- LHA- and ZIP-packed modules open on Windows only: the macOS and Linux builds have no unpackers for them.
- The macOS standalone app has not been built here (the Mac VM has the command line tools, not Xcode). With no one logged in to the VM's desktop, `auval` has not run and the macOS editor checks ran offscreen.
- On Linux the file dialogs need zenity or kdialog; without either the tracker's Load and Save do nothing.
- Only the current tab follows the host. MilkyTracker's other tabs play as they would on their own.
- DAW Sync places the song by its starting speed; a song that changes speed partway lands a little off when the host jumps into it.
