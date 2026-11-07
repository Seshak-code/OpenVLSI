// os_input: mouse and keyboard from outside the app, through the operating system's own event stream, as a person's hands are.
// macOS only delivers them when this process's parent (Terminal, say) has Accessibility permission, in System Settings > Privacy &
// Security > Accessibility; `os_input trusted` says, and without it the events would vanish silently, so every other action refuses.
//   os_input trusted | move X Y | click X Y [right] | double X Y | drag X0 Y0 X1 Y1 | wheel X Y DELTA | key KEYCODE [shift] [cmd] [ctrl] [alt]
#include <ApplicationServices/ApplicationServices.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

static void post(CGEventRef e) {
    CGEventPost(kCGHIDEventTap, e);
    CFRelease(e);
    usleep(25000);  // a hand is not instant: menus and focus follow between events
}

static void mouse(CGEventType type, CGPoint at, CGMouseButton button, int clicks = 1) {
    CGEventRef e = CGEventCreateMouseEvent(nullptr, type, at, button);
    CGEventSetIntegerValueField(e, kCGMouseEventClickState, clicks);
    post(e);
}

int main(int argc, char** argv) {
    const std::string action = argc > 1 ? argv[1] : "";
    const bool trusted = AXIsProcessTrusted();
    if (action == "trusted") {
        std::puts(trusted ? "yes" : "no");
        return 0;
    }
    if (!trusted) {
        std::fprintf(stderr, "os_input: no Accessibility permission: add this terminal in System Settings > Privacy & Security > Accessibility\n");
        return 3;
    }
    const auto number = [&](int k) { return k < argc ? std::atof(argv[k]) : 0.0; };
    const CGPoint at = CGPointMake(number(2), number(3));
    if (action == "move") {
        mouse(kCGEventMouseMoved, at, kCGMouseButtonLeft);
    } else if (action == "click") {
        const bool right = argc > 4 && !std::strcmp(argv[4], "right");
        mouse(kCGEventMouseMoved, at, kCGMouseButtonLeft);
        mouse(right ? kCGEventRightMouseDown : kCGEventLeftMouseDown, at, right ? kCGMouseButtonRight : kCGMouseButtonLeft);
        mouse(right ? kCGEventRightMouseUp : kCGEventLeftMouseUp, at, right ? kCGMouseButtonRight : kCGMouseButtonLeft);
    } else if (action == "double") {
        mouse(kCGEventMouseMoved, at, kCGMouseButtonLeft);
        for (int c = 1; c <= 2; ++c) {
            mouse(kCGEventLeftMouseDown, at, kCGMouseButtonLeft, c);
            mouse(kCGEventLeftMouseUp, at, kCGMouseButtonLeft, c);
        }
    } else if (action == "drag") {
        const CGPoint to = CGPointMake(number(4), number(5));
        mouse(kCGEventMouseMoved, at, kCGMouseButtonLeft);
        mouse(kCGEventLeftMouseDown, at, kCGMouseButtonLeft);
        for (int k = 1; k <= 8; ++k) mouse(kCGEventLeftMouseDragged, CGPointMake(at.x + (to.x - at.x) * k / 8, at.y + (to.y - at.y) * k / 8), kCGMouseButtonLeft);
        mouse(kCGEventLeftMouseUp, to, kCGMouseButtonLeft);
    } else if (action == "wheel") {
        mouse(kCGEventMouseMoved, at, kCGMouseButtonLeft);
        post(CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitLine, 1, (int)number(4) / 40));
    } else if (action == "key") {
        CGEventFlags flags = 0;
        for (int k = 3; k < argc; ++k) {
            if (!std::strcmp(argv[k], "shift")) flags |= kCGEventFlagMaskShift;
            if (!std::strcmp(argv[k], "cmd")) flags |= kCGEventFlagMaskCommand;
            if (!std::strcmp(argv[k], "ctrl")) flags |= kCGEventFlagMaskControl;
            if (!std::strcmp(argv[k], "alt")) flags |= kCGEventFlagMaskAlternate;
        }
        for (bool down : {true, false}) {
            CGEventRef e = CGEventCreateKeyboardEvent(nullptr, (CGKeyCode)std::atoi(argv[2]), down);
            CGEventSetFlags(e, flags);
            post(e);
        }
    } else {
        std::fprintf(stderr, "usage: os_input trusted | move X Y | click X Y [right] | double X Y | drag X0 Y0 X1 Y1 | wheel X Y DELTA | key KEYCODE [shift] [cmd] [ctrl] [alt]\n");
        return 2;
    }
    return 0;
}
