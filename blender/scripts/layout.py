# The main window's layout at 1x, in window pixels (x right, y down): what build_chrome.py cuts into the chrome
# and where plugin/skin/views/main.json puts the tracker, the right column and the keyboard. Move something here,
# render the chrome again and move the widget to match.
W, H = 1315, 949

DEPTH = 6            # every well is cut this deep, with 45 degree walls as wide as it is deep
RADIUS = 10          # the wells' corner radius at the surface

TRACKER = (1024, 720)                                                  # MilkyTracker's screen, pixel for pixel at 1x
TRACKWELL = (8, 8, TRACKER[0] + 2 * DEPTH, TRACKER[1] + 2 * DEPTH)     # its well; the screen covers the whole floor
SCREEN = (TRACKWELL[0] + DEPTH, TRACKWELL[1] + DEPTH, TRACKER[0], TRACKER[1])
COLUMN = (TRACKWELL[0] + TRACKWELL[2] + 8, 8, 255, TRACKWELL[3])       # the right column: mode, sync, trigger, output
ZONES_H = 20                                                           # MIDI Trigger's zone strip under the keys
KEYWELL = (8, TRACKWELL[1] + TRACKWELL[3] + 8, W - 16, 2 * DEPTH + 2 + 161 + 2 + ZONES_H)
# The keyboard: 73 keys, C1 to C7, 195 px an octave at the skin's density 1.5 (the piano kind's 130 px), the top C 28
KEYS_FIRST, KEYS_COUNT = 24, 73
KEYS_W, KEYS_H = 6 * 195 + 28, 161
KEYS = (KEYWELL[0] + (KEYWELL[2] - KEYS_W) // 2, KEYWELL[1] + DEPTH + 2, KEYS_W, KEYS_H)
ZONES = (KEYS[0], KEYS[1] + KEYS_H + 2, KEYS_W, ZONES_H)
# The patterns dialog, a modal the runtime centres in the window: its plate of chrome
DIALOG_W, DIALOG_H = 460, 360
DIALOG = ((W - DIALOG_W) // 2, (H - DIALOG_H) // 2, DIALOG_W, DIALOG_H)

assert COLUMN[0] + COLUMN[2] + 8 == W, 'the column runs past the window'
assert KEYWELL[1] + KEYWELL[3] + 4 == H, 'the keyboard runs past the window'
