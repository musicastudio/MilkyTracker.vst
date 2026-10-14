// macOS: a flipped NSView that presents the GUI canvas through a CGImage and feeds it input.
// The view class is registered at runtime under a name unique to this binary, so two Hollow
// plug-ins loaded in one host never share (or fight over) an Objective-C class.
// Works with and without ARC.
#include "../core/core.h"
#import <Cocoa/Cocoa.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <cmath>
#include <cstdio>

namespace hollow {

struct PlatformWindow {
    void* view = nullptr;   // NSView*, retained
    void* timer = nullptr;  // NSTimer*, owned by the run loop until invalidated
    Gui* gui = nullptr;
    int pick = -1;          // menu choice, set by the menu item action
};

static Ivar windowIvar;

static PlatformWindow*& windowOf(id view) {
    return *(PlatformWindow**)((char*)(__bridge void*)view + ivar_getOffset(windowIvar));
}

static void mouse(id self, NSEvent* e, int kind) {   // 0 down, 1 drag/move, 2 up, 3 right down, 4 right up
    PlatformWindow* w = windowOf(self);
    if (!w) return;
    NSPoint p = [self convertPoint:[e locationInWindow] fromView:nil];
    int s = w->gui->scale();
    int x = (int)std::floor(p.x / s), y = (int)std::floor(p.y / s);
    bool shift = ([e modifierFlags] & NSEventModifierFlagShift) != 0;
    if (kind == 0) w->gui->mouseDown(x, y, false, [e clickCount] == 2, shift);
    else if (kind == 1) w->gui->mouseMove(x, y, shift);
    else if (kind == 2) w->gui->mouseUp(x, y, shift);
    else if (kind == 3) w->gui->mouseDown(x, y, true, false, shift);
    else w->gui->rightUp(x, y, shift);
}

static BOOL yes(id, SEL) { return YES; }
static BOOL yesEvent(id, SEL, NSEvent*) { return YES; }
static void mouseDown(id self, SEL, NSEvent* e) { mouse(self, e, 0); }
static void mouseDragged(id self, SEL, NSEvent* e) { mouse(self, e, 1); }
static void mouseUp(id self, SEL, NSEvent* e) { mouse(self, e, 2); }
static void rightMouseDown(id self, SEL, NSEvent* e) { mouse(self, e, 3); }
static void rightMouseUp(id self, SEL, NSEvent* e) { mouse(self, e, 4); }

static void mouseExited(id self, SEL, NSEvent*) {
    if (PlatformWindow* w = windowOf(self)) w->gui->mouseLeave();
}

static void scrollWheel(id self, SEL, NSEvent* e) {
    PlatformWindow* w = windowOf(self);
    if (!w) return;
    NSPoint p = [self convertPoint:[e locationInWindow] fromView:nil];
    int s = w->gui->scale();
    double notches = [e scrollingDeltaY];
    if ([e hasPreciseScrollingDeltas]) notches /= 10;   // Shortcut: 10 points of trackpad scroll = one notch
    w->gui->wheel((int)std::floor(p.x / s), (int)std::floor(p.y / s), notches, ([e modifierFlags] & NSEventModifierFlagShift) != 0);
}

static void pickItem(id self, SEL, id sender) {
    if (PlatformWindow* w = windowOf(self)) w->pick = (int)[sender tag];
}

// The editor holds the keyboard while its window is up, so a skin's "keys" shortcuts work without a click
// first. It consumes only what it has a use for; the rest goes to the next responder, so the host keeps its
// own shortcuts. A skin with no bindings is as it was: the view takes the keyboard only while something in
// the editor wants it.
static BOOL acceptsFirstResponder(id self, SEL) {
    PlatformWindow* w = windowOf(self);
    return w && (w->gui->wantsKeys() || !w->gui->skin().keys.empty());
}

static BOOL resignFirstResponder(id self, SEL) {
    if (PlatformWindow* w = windowOf(self)) w->gui->focusLost();
    return YES;
}

// The keys the editor knows by name, by their virtual key code; a letter or a digit is not one of them.
static Key keyOfCode(NSInteger code) {
    switch (code) {
    case 123: return KeyLeft;
    case 124: return KeyRight;
    case 126: return KeyUp;
    case 125: return KeyDown;
    case 115: return KeyHome;
    case 119: return KeyEnd;
    case 51: return KeyBackspace;
    case 117: return KeyDelete;
    case 36: case 76: return KeyEnter;
    case 53: return KeyEscape;
    case 48: return KeyTab;
    default: return KeyNone;
    }
}

// ---- raw keys (Gui::rawKey) ------------------------------------------------------------------------------
// A key as Windows reports it, which is what PixelInput carries: by the Mac's virtual key code, the PC's scan
// code at the same place (set 1, the extended bit as 0x100) and the virtual key Windows gives it. Command is a
// Ctrl key here, as it is to the editor (keyDown's cmd), the right-hand ones extended; a letter's virtual key
// is what the layout types on it, as on Windows.
struct RawKey { unsigned short scan, vk; };
static RawKey rawKeyOf(unsigned short code) {
    static const unsigned short t[][3] = {
        {0x00, 0x1e, 'A'}, {0x01, 0x1f, 'S'}, {0x02, 0x20, 'D'}, {0x03, 0x21, 'F'}, {0x04, 0x23, 'H'}, {0x05, 0x22, 'G'},
        {0x06, 0x2c, 'Z'}, {0x07, 0x2d, 'X'}, {0x08, 0x2e, 'C'}, {0x09, 0x2f, 'V'}, {0x0a, 0x56, 0xe2}, {0x0b, 0x30, 'B'},
        {0x0c, 0x10, 'Q'}, {0x0d, 0x11, 'W'}, {0x0e, 0x12, 'E'}, {0x0f, 0x13, 'R'}, {0x10, 0x15, 'Y'}, {0x11, 0x14, 'T'},
        {0x12, 0x02, '1'}, {0x13, 0x03, '2'}, {0x14, 0x04, '3'}, {0x15, 0x05, '4'}, {0x16, 0x07, '6'}, {0x17, 0x06, '5'},
        {0x18, 0x0d, 0xbb}, {0x19, 0x0a, '9'}, {0x1a, 0x08, '7'}, {0x1b, 0x0c, 0xbd}, {0x1c, 0x09, '8'}, {0x1d, 0x0b, '0'},
        {0x1e, 0x1b, 0xdd}, {0x1f, 0x18, 'O'}, {0x20, 0x16, 'U'}, {0x21, 0x1a, 0xdb}, {0x22, 0x17, 'I'}, {0x23, 0x19, 'P'},
        {0x24, 0x1c, 0x0d}, {0x25, 0x26, 'L'}, {0x26, 0x24, 'J'}, {0x27, 0x28, 0xde}, {0x28, 0x25, 'K'}, {0x29, 0x27, 0xba},
        {0x2a, 0x2b, 0xdc}, {0x2b, 0x33, 0xbc}, {0x2c, 0x35, 0xbf}, {0x2d, 0x31, 'N'}, {0x2e, 0x32, 'M'}, {0x2f, 0x34, 0xbe},
        {0x30, 0x0f, 0x09}, {0x31, 0x39, 0x20}, {0x32, 0x29, 0xc0}, {0x33, 0x0e, 0x08}, {0x35, 0x01, 0x1b},
        {0x36, 0x11d, 0x11}, {0x37, 0x1d, 0x11}, {0x38, 0x2a, 0x10}, {0x39, 0x3a, 0x14}, {0x3a, 0x38, 0x12}, {0x3b, 0x1d, 0x11},
        {0x3c, 0x36, 0x10}, {0x3d, 0x138, 0x12}, {0x3e, 0x11d, 0x11},
        {0x41, 0x53, 0x6e}, {0x43, 0x37, 0x6a}, {0x45, 0x4e, 0x6b}, {0x47, 0x45, 0x90}, {0x4b, 0x135, 0x6f}, {0x4c, 0x11c, 0x0d},
        {0x4e, 0x4a, 0x6d}, {0x51, 0x59, 0x92}, {0x52, 0x52, 0x60}, {0x53, 0x4f, 0x61}, {0x54, 0x50, 0x62}, {0x55, 0x51, 0x63},
        {0x56, 0x4b, 0x64}, {0x57, 0x4c, 0x65}, {0x58, 0x4d, 0x66}, {0x59, 0x47, 0x67}, {0x5b, 0x48, 0x68}, {0x5c, 0x49, 0x69},
        {0x60, 0x3f, 0x74}, {0x61, 0x40, 0x75}, {0x62, 0x41, 0x76}, {0x63, 0x3d, 0x72}, {0x64, 0x42, 0x77}, {0x65, 0x43, 0x78},
        {0x67, 0x57, 0x7a}, {0x69, 0x64, 0x7c}, {0x6b, 0x65, 0x7d}, {0x6d, 0x44, 0x79}, {0x6f, 0x58, 0x7b}, {0x71, 0x66, 0x7e},
        {0x72, 0x152, 0x2d}, {0x73, 0x147, 0x24}, {0x74, 0x149, 0x21}, {0x75, 0x153, 0x2e}, {0x76, 0x3e, 0x73}, {0x77, 0x14f, 0x23},
        {0x78, 0x3c, 0x71}, {0x79, 0x151, 0x22}, {0x7a, 0x3b, 0x70}, {0x7b, 0x14b, 0x25}, {0x7c, 0x14d, 0x27}, {0x7d, 0x150, 0x28},
        {0x7e, 0x148, 0x26}};
    for (auto& k : t)
        if (k[0] == code) return {k[1], k[2]};
    return {0, 0};
}

// A modifier key's own bit among the event's device-dependent flags (IOKit's NX_DEVICE*KEYMASK): set while it is down.
static NSEventModifierFlags deviceBit(unsigned short code) {
    switch (code) {
    case 0x3b: return 0x1;      // Control
    case 0x38: return 0x2;      // Shift
    case 0x3c: return 0x4;      // right Shift
    case 0x37: return 0x8;      // Command
    case 0x36: return 0x10;     // right Command
    case 0x3a: return 0x20;     // Option
    case 0x3d: return 0x40;     // right Option
    case 0x3e: return 0x2000;   // right Control
    default: return 0;
    }
}

static bool offerRaw(PlatformWindow* w, NSEvent* ev, bool down) {
    Gui& g = *w->gui;
    if (!g.rawFocus()) return false;
    PixelInput e;
    e.type = down ? PixelInput::KeyDown : PixelInput::KeyUp;
    const RawKey k = rawKeyOf([ev keyCode]);
    e.scan = k.scan;
    e.vk = k.vk;
    const NSEventModifierFlags m = [ev modifierFlags];
    e.shift = (m & NSEventModifierFlagShift) != 0;
    e.ctrl = (m & (NSEventModifierFlagCommand | NSEventModifierFlagControl)) != 0;
    e.alt = (m & NSEventModifierFlagOption) != 0;
    if (down && [ev type] != NSEventTypeFlagsChanged) {
        NSString* bare = [ev charactersIgnoringModifiers];   // Shift still applies to it
        if ([bare length] == 1) {
            const unichar c = [[bare lowercaseString] characterAtIndex:0];
            if (c >= 'a' && c <= 'z') e.vk = c - 'a' + 'A';
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (!e.shift && c >= 0x20 && c < 0x7f)) e.plain = c;
        }
        NSString* s = [ev characters];
        if ([s length] == 1) {
            const unichar c = [s characterAtIndex:0];
            if (c >= 0x20 && c != 0x7f && (c < 0xf700 || c > 0xf8ff)) e.ch = c;
        }
    }
    return g.rawKey(e);
}

static void keyUp(id self, SEL, NSEvent* e) {
    PlatformWindow* w = windowOf(self);
    if (!w || !offerRaw(w, e, false)) [[self nextResponder] keyUp:e];
}

// A modifier key pressed or let go alone; Caps Lock reports its new state, so it is a press and a release at once.
static void flagsChanged(id self, SEL, NSEvent* e) {
    PlatformWindow* w = windowOf(self);
    bool used = false;
    if (w && [e keyCode] == 0x39) {
        used = offerRaw(w, e, true);
        offerRaw(w, e, false);
    } else if (w) {
        used = offerRaw(w, e, ([e modifierFlags] & deviceBit([e keyCode])) != 0);
    }
    if (!used) [[self nextResponder] flagsChanged:e];
}

// A Command chord reaches the view here before the host's menus see it: while the keyboard is the widget's,
// the chord is a key like any other.
static BOOL performKeyEquivalent(id self, SEL, NSEvent* e) {
    PlatformWindow* w = windowOf(self);
    if (w && [[self window] firstResponder] == self && [e type] == NSEventTypeKeyDown && offerRaw(w, e, true)) return YES;
    struct objc_super sup = {self, [NSView class]};
    return ((BOOL (*)(struct objc_super*, SEL, NSEvent*))objc_msgSendSuper)(&sup, @selector(performKeyEquivalent:), e);
}

// Shortcut: characters come from the key event itself, not the text input system, so there is no
// input method (dead keys still compose, since the event carries the composed characters).
static void keyDown(id self, SEL, NSEvent* e) {
    PlatformWindow* w = windowOf(self);
    if (!w) {
        [[self nextResponder] keyDown:e];
        return;
    }
    if (offerRaw(w, e, true)) return;
    NSEventModifierFlags m = [e modifierFlags];
    bool shift = (m & NSEventModifierFlagShift) != 0;
    bool cmd = (m & (NSEventModifierFlagCommand | NSEventModifierFlagControl)) != 0;
    bool alt = (m & NSEventModifierFlagOption) != 0;
    Key k = keyOfCode([e keyCode]);
    if (!w->gui->wantsKeys()) {   // only a chord in the skin's bindings is ours; the host gets everything else
        // The physical key, so Alt+F is 'F' and not the ƒ that Option+F would type.
        unsigned ch = 0;
        NSString* bare = [e charactersIgnoringModifiers];
        if ([bare length] == 1) {
            unichar c = [[bare uppercaseString] characterAtIndex:0];
            if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) ch = c;
        }
        if (!w->gui->keyDown(k, shift, cmd, alt, ch)) [[self nextResponder] keyDown:e];
        return;
    }
    if (cmd && [[[e charactersIgnoringModifiers] lowercaseString] isEqualToString:@"a"]) k = KeySelectAll;
    if (k != KeyNone) {
        w->gui->keyDown(k, shift, cmd);
        return;
    }
    if (cmd) return;
    NSString* s = [e characters];
    for (NSUInteger i = 0; i < [s length]; ++i) {
        unichar c = [s characterAtIndex:i];
        if (c >= 0xf700 && c <= 0xf8ff) continue;   // function keys
        if (c >= 0xd800 && c < 0xdc00 && i + 1 < [s length]) {
            unichar d = [s characterAtIndex:++i];
            w->gui->keyChar(0x10000 + ((c - 0xd800u) << 10) + (d - 0xdc00u));
        } else {
            w->gui->keyChar(c);
        }
    }
}

static void selectAll(id self, SEL, id) {   // Edit > Select All of the host's menu bar
    if (PlatformWindow* w = windowOf(self)) w->gui->keyDown(KeySelectAll, false, true);
}

static void drawRect(id self, SEL, NSRect) {
    PlatformWindow* w = windowOf(self);
    if (!w) return;
    Gui& g = *w->gui;
    const std::vector<uint32_t>& px = g.pixels();
    CGContextRef ctx = [[NSGraphicsContext currentContext] CGContext];
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGDataProviderRef dp = CGDataProviderCreateWithData(nullptr, px.data(), px.size() * 4, nullptr);
    CGImageRef img = CGImageCreate(g.width(), g.height(), 8, 32, g.width() * 4, cs,
                                   (CGBitmapInfo)kCGBitmapByteOrder32Little | (CGBitmapInfo)kCGImageAlphaNoneSkipFirst, dp, nullptr, false, kCGRenderingIntentDefault);
    int s = g.scale();
    CGContextSaveGState(ctx);
    CGContextSetInterpolationQuality(ctx, kCGInterpolationNone);   // pixel replication
    CGContextTranslateCTM(ctx, 0, g.height() * s);                  // the view is flipped, images are not
    CGContextScaleCTM(ctx, 1, -1);
    CGContextDrawImage(ctx, CGRectMake(0, 0, g.width() * s, g.height() * s), img);
    CGContextRestoreGState(ctx);
    CGImageRelease(img);
    CGDataProviderRelease(dp);
    CGColorSpaceRelease(cs);
}

static Class viewClass() {
    static Class cls = nil;
    if (cls) return cls;
    char name[64];
    std::snprintf(name, sizeof name, "HollowView_%p", (void*)&viewClass);   // unique per loaded binary
    cls = objc_allocateClassPair([NSView class], name, 0);
    class_addIvar(cls, "hollowWindow", sizeof(void*), (uint8_t)std::log2(sizeof(void*)), "^v");
    auto types = [](SEL s) { return method_getTypeEncoding(class_getInstanceMethod([NSView class], s)); };
    class_addMethod(cls, @selector(isFlipped), (IMP)yes, types(@selector(isFlipped)));
    class_addMethod(cls, @selector(acceptsFirstMouse:), (IMP)yesEvent, types(@selector(acceptsFirstMouse:)));
    class_addMethod(cls, @selector(drawRect:), (IMP)drawRect, types(@selector(drawRect:)));
    class_addMethod(cls, @selector(mouseDown:), (IMP)mouseDown, types(@selector(mouseDown:)));
    class_addMethod(cls, @selector(mouseDragged:), (IMP)mouseDragged, types(@selector(mouseDragged:)));
    class_addMethod(cls, @selector(mouseMoved:), (IMP)mouseDragged, types(@selector(mouseMoved:)));
    class_addMethod(cls, @selector(mouseUp:), (IMP)mouseUp, types(@selector(mouseUp:)));
    class_addMethod(cls, @selector(rightMouseDown:), (IMP)rightMouseDown, types(@selector(rightMouseDown:)));
    class_addMethod(cls, @selector(rightMouseUp:), (IMP)rightMouseUp, types(@selector(rightMouseUp:)));
    class_addMethod(cls, @selector(mouseExited:), (IMP)mouseExited, types(@selector(mouseExited:)));
    class_addMethod(cls, @selector(scrollWheel:), (IMP)scrollWheel, types(@selector(scrollWheel:)));
    class_addMethod(cls, @selector(acceptsFirstResponder), (IMP)acceptsFirstResponder, types(@selector(acceptsFirstResponder)));
    class_addMethod(cls, @selector(resignFirstResponder), (IMP)resignFirstResponder, types(@selector(resignFirstResponder)));
    class_addMethod(cls, @selector(keyDown:), (IMP)keyDown, types(@selector(keyDown:)));
    class_addMethod(cls, @selector(keyUp:), (IMP)keyUp, types(@selector(keyUp:)));
    class_addMethod(cls, @selector(flagsChanged:), (IMP)flagsChanged, types(@selector(flagsChanged:)));
    class_addMethod(cls, @selector(performKeyEquivalent:), (IMP)performKeyEquivalent, types(@selector(performKeyEquivalent:)));
    class_addMethod(cls, @selector(selectAll:), (IMP)selectAll, "v@:@");
    class_addMethod(cls, NSSelectorFromString(@"hollowPick:"), (IMP)pickItem, "v@:@");
    objc_registerClassPair(cls);
    windowIvar = class_getInstanceVariable(cls, "hollowWindow");
    return cls;
}

PlatformWindow* platformOpen(void* parent, Gui* gui) {
    NSView* p = (__bridge NSView*)parent;
    if (!p) return nullptr;
    auto* w = new PlatformWindow;
    w->gui = gui;
    NSView* v = [[viewClass() alloc] initWithFrame:NSMakeRect(0, 0, gui->width() * gui->scale(), gui->height() * gui->scale())];
    windowOf(v) = w;
    NSTrackingArea* area = [[NSTrackingArea alloc] initWithRect:NSZeroRect
                                                        options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways | NSTrackingInVisibleRect
                                                          owner:v
                                                       userInfo:nil];
    [v addTrackingArea:area];
    [p addSubview:v];
    w->view = (void*)CFBridgingRetain(v);
#if !__has_feature(objc_arc)
    [area release];
    [v release];
#endif
    NSTimer* t = [NSTimer timerWithTimeInterval:1.0 / 30 repeats:YES block:^(NSTimer* timer) { (void)timer; gui->tick(); }];
    [[NSRunLoop currentRunLoop] addTimer:t forMode:NSRunLoopCommonModes];
    w->timer = (__bridge void*)t;
    if (!gui->skin().keys.empty()) platformFocus(w, true);   // a skin with shortcuts holds the keyboard while it is up
    return w;
}

void platformClose(PlatformWindow* w) {
    [(__bridge NSTimer*)w->timer invalidate];
    NSView* v = (__bridge NSView*)w->view;
    windowOf(v) = nullptr;   // late events find no window
    [v removeFromSuperview];
    (void)CFBridgingRelease(w->view);
    delete w;
}

const bool kNativeMenus = true;
void platformHold(PlatformWindow*, bool) {}   // the GUI runs on the host's thread

void platformInvalidate(PlatformWindow* w, Rect r) {
    int s = w->gui->scale();
    [(__bridge NSView*)w->view setNeedsDisplayInRect:NSMakeRect(r.x * s, r.y * s, r.w * s, r.h * s)];
}

void platformSize(PlatformWindow* w, int width, int height) {
    NSView* v = (__bridge NSView*)w->view;
    [v setFrameSize:NSMakeSize(width, height)];
    [v setNeedsDisplay:YES];
}

// Entries report their id through the item tag; submenus nest. Limit: NSMenu has no columns, so
// column breaks are ignored.
static NSMenu* buildMenu(NSView* v, const std::vector<MenuEntry>& items) {
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@""];
    [menu setAutoenablesItems:NO];
    for (auto& e : items) {
        if (e.separator) { [menu addItem:[NSMenuItem separatorItem]]; continue; }
        NSString* title = [NSString stringWithUTF8String:e.label.c_str()];
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:(title ? title : @"?") action:NSSelectorFromString(@"hollowPick:") keyEquivalent:@""];
        [item setTarget:v];
        [item setTag:(NSInteger)e.id];
        [item setEnabled:YES];
        [item setState:e.checked ? NSControlStateValueOn : NSControlStateValueOff];
        if (!e.items.empty()) {
            NSMenu* sub = buildMenu(v, e.items);
            [item setSubmenu:sub];
#if !__has_feature(objc_arc)
            [sub release];
#endif
        }
        [menu addItem:item];
#if !__has_feature(objc_arc)
        [item release];
#endif
    }
    return menu;
}

int platformMenu(PlatformWindow* w, const std::vector<MenuEntry>& items, int x, int y) {
    NSView* v = (__bridge NSView*)w->view;
    NSMenu* menu = buildMenu(v, items);
    w->pick = -1;
    int s = w->gui->scale();
    [menu popUpMenuPositioningItem:nil atLocation:NSMakePoint(x * s, y * s) inView:v];   // returns after the choice
#if !__has_feature(objc_arc)
    [menu release];
#endif
    return w->pick;
}

void platformTip(PlatformWindow* w, const std::string& text) {
    NSString* s = text.empty() ? nil : [NSString stringWithUTF8String:text.c_str()];
    [(__bridge NSView*)w->view setToolTip:s];
}

// Limit: on macOS the host sizes the view's superviews itself; nothing is walked here.
void platformResizeParents(PlatformWindow*, int, int) {}

void platformFocus(PlatformWindow* w, bool on) {
    NSView* v = (__bridge NSView*)w->view;
    NSWindow* win = [v window];
    if (on) [win makeFirstResponder:v];
    else if ([win firstResponder] == v) [win makeFirstResponder:nil];
}

// Not inverted here: the view reads the physical key off charactersIgnoringModifiers already, so a chord
// does not have to be recovered from the character a host handed over.
unsigned platformKeyChar(unsigned, bool* shift) {
    if (shift) *shift = false;
    return 0;
}

void platformOpenUrl(const std::string& url) {
    if (url.compare(0, 7, "http://") != 0 && url.compare(0, 8, "https://") != 0) return;
    NSString* s = [NSString stringWithUTF8String:url.c_str()];
    NSURL* u = s ? [NSURL URLWithString:s] : nil;
    if (u) [[NSWorkspace sharedWorkspace] openURL:u];
}

// clap-wrapper's standalone app delegate answers its Audio Settings menu item with this action.
const bool kAudioSettings = true;
void platformAudioSettings(PlatformWindow*) {
    [NSApp sendAction:NSSelectorFromString(@"openAudioSettingsWindow:") to:nil from:nil];
}
void platformDriverPanel(PlatformWindow*) {}
// Shortcut: the settings stay in the app's own window, and closing does not ask; Windows has both.
bool platformDevices(PlatformWindow*, std::vector<DeviceList>&) { return false; }
void platformSetDevice(PlatformWindow*, int, int) {}
void platformWatchClose(PlatformWindow*) {}
void platformCloseApp(PlatformWindow*) {}

// NSOpenPanel / NSSavePanel, run modal; the types' extensions become the allowed ones.
bool platformFileDialog(PlatformWindow*, bool save, const std::string& title, const Vars& types, const std::string& name, std::string& path) {
    NSMutableArray* exts = [NSMutableArray array];
    for (auto& t : types) {
        size_t a = 0;
        while (a <= t.second.size()) {
            size_t b = t.second.find(';', a);
            std::string e = t.second.substr(a, b == std::string::npos ? std::string::npos : b - a);
            if (!e.empty()) [exts addObject:[NSString stringWithUTF8String:e.c_str()]];
            if (b == std::string::npos) break;
            a = b + 1;
        }
    }
    NSSavePanel* panel = save ? [NSSavePanel savePanel] : [NSOpenPanel openPanel];
    if (!title.empty()) [panel setTitle:[NSString stringWithUTF8String:title.c_str()]];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    if ([exts count]) [panel setAllowedFileTypes:exts];
#pragma clang diagnostic pop
    if (save && !name.empty()) [panel setNameFieldStringValue:[NSString stringWithUTF8String:name.c_str()]];
    if ([panel runModal] != NSModalResponseOK) return false;
    NSURL* u = [panel URL];
    const char* p = u ? [[u path] UTF8String] : nullptr;
    if (!p) return false;
    path = p;
    return true;
}

} // namespace hollow
