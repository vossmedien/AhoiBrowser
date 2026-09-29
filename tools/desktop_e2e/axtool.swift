import AppKit
// PID-scoped Accessibility helper for isolated Ahoi E2E clones.
// usage: axtool dump <pid> [depth]
//        axtool enable <pid>
//        axtool press <pid> <substring-of-title|description|identifier>
//        axtool type <pid> <text>
//        axtool key <pid> <virtualKeyCode> [cmd|shift|opt|ctrl ...]
//        axtool hidscroll <pid> <element> <dy> [dx] [cmd|shift|opt|ctrl ...]
//        axtool hidmiddle <pid> <element> <dx> <dy>
import ApplicationServices
import Foundation

func attr(_ e: AXUIElement, _ name: String) -> AnyObject? {
    var v: AnyObject?
    return AXUIElementCopyAttributeValue(e, name as CFString, &v) == .success ? v : nil
}
func str(_ e: AXUIElement, _ name: String) -> String {
    guard let v = attr(e, name) else { return "" }
    if let s = v as? String { return s }
    if let n = v as? NSNumber { return n.stringValue }
    return ""
}
func children(_ e: AXUIElement) -> [AXUIElement] {
    (attr(e, kAXChildrenAttribute) as? [AXUIElement]) ?? []
}
// Values are cut to keep dumps short; AHOI_AX_VALUE_MAX reads longer text.
let valueLimit = Int(ProcessInfo.processInfo.environment["AHOI_AX_VALUE_MAX"] ?? "") ?? 80
func label(_ e: AXUIElement) -> String {
    [str(e, kAXRoleAttribute), str(e, kAXTitleAttribute), str(e, kAXDescriptionAttribute),
     String(str(e, kAXValueAttribute).prefix(valueLimit)), str(e, "AXIdentifier")]
        .filter { !$0.isEmpty }.joined(separator: " | ")
}
func walk(_ e: AXUIElement, _ d: Int, _ max: Int, _ visit: (AXUIElement, Int) -> Bool) -> Bool {
    if visit(e, d) { return true }
    guard d < max else { return false }
    for c in children(e) where walk(c, d + 1, max, visit) { return true }
    return false
}

/// `needle` is a title/description/identifier, optionally prefixed with a role
/// (`AXButton:Anmelden`) when a window and a button share one name.
func matches(_ e: AXUIElement, _ needle: String) -> Bool {
    var name = needle
    if needle.hasPrefix("AX"), let colon = needle.firstIndex(of: ":") {
        guard str(e, kAXRoleAttribute) == String(needle[..<colon]) else { return false }
        name = String(needle[needle.index(after: colon)...])
    }
    return [str(e, kAXTitleAttribute), str(e, kAXDescriptionAttribute), str(e, "AXIdentifier")]
        .contains(name)
}

/// Center of the first element matching `needle`, in global coordinates.
func centerOf(_ app: AXUIElement, _ needle: String) -> CGPoint? {
    var found: AXUIElement?
    _ = walk(app, 0, 30) { e, _ in
        if matches(e, needle) { found = e; return true }
        return false
    }
    guard let f = found, let pv = attr(f, kAXPositionAttribute),
          let sv = attr(f, kAXSizeAttribute) else { return nil }
    var pos = CGPoint.zero, size = CGSize.zero
    AXValueGetValue(pv as! AXValue, .cgPoint, &pos)
    AXValueGetValue(sv as! AXValue, .cgSize, &size)
    return CGPoint(x: pos.x + size.width / 2, y: pos.y + size.height / 2)
}

func flagsFrom(_ names: ArraySlice<String>) -> CGEventFlags {
    var flags: CGEventFlags = []
    for m in names {
        switch m {
        case "cmd": flags.insert(.maskCommand)
        case "shift": flags.insert(.maskShift)
        case "opt": flags.insert(.maskAlternate)
        case "ctrl": flags.insert(.maskControl)
        default: break
        }
    }
    return flags
}

let args = CommandLine.arguments
guard args.count >= 3, let pid = pid_t(args[2]) else {
    FileHandle.standardError.write("bad args\n".data(using: .utf8)!); exit(2)
}
guard AXIsProcessTrusted() else {
    FileHandle.standardError.write("not AX trusted\n".data(using: .utf8)!); exit(3)
}
let app = AXUIElementCreateApplication(pid)
switch args[1] {
case "enable":
    let r1 = AXUIElementSetAttributeValue(app, "AXManualAccessibility" as CFString, kCFBooleanTrue)
    let r2 = AXUIElementSetAttributeValue(app, "AXEnhancedUserInterface" as CFString, kCFBooleanTrue)
    print("manual=\(r1.rawValue) enhanced=\(r2.rawValue)")
case "dump":
    let depth = args.count > 3 ? Int(args[3]) ?? 12 : 12
    var n = 0
    _ = walk(app, 0, depth) { e, d in
        n += 1; print(String(repeating: "  ", count: d) + label(e)); return n > 1500
    }
case "press":
    let needle = args[3]
    var found: AXUIElement?
    // Location-bar controls sit deeper than 30 levels (build 39 AX dump).
    _ = walk(app, 0, 45) { e, _ in
        if matches(e, needle) { found = e; return true }
        return false
    }
    guard let f = found else { print("NOT FOUND"); exit(1) }
    let action = args.count > 4 ? args[4] : kAXPressAction
    AXUIElementSetMessagingTimeout(f, 1.5)
    print("\(action) \(label(f)) -> \(AXUIElementPerformAction(f, action as CFString).rawValue)")
case "checked":
    // Exit 0 when the menu item carries a check mark (native NSMenu state).
    var found: AXUIElement?
    _ = walk(app, 0, 30) { e, _ in
        if matches(e, args[3]) { found = e; return true }
        return false
    }
    guard let f = found else { print("NOT FOUND"); exit(1) }
    let mark = str(f, "AXMenuItemMarkChar")
    print("checked \(label(f)) mark=\(mark)")
    exit(mark.isEmpty ? 1 : 0)
case "type":
    let text = args[3]
    for scalar in text.utf16 {
        var ch = scalar
        for down in [true, false] {
            let ev = CGEvent(keyboardEventSource: nil, virtualKey: 0, keyDown: down)!
            ev.keyboardSetUnicodeString(stringLength: 1, unicodeString: &ch)
            ev.postToPid(pid)
        }
        usleep(15000)
    }
    print("typed \(text.count)")
case "key":
    let code = CGKeyCode(args[3])!
    var flags: CGEventFlags = []
    for m in args.dropFirst(4) {
        switch m { case "cmd": flags.insert(.maskCommand); case "shift": flags.insert(.maskShift)
        case "opt": flags.insert(.maskAlternate); case "ctrl": flags.insert(.maskControl); default: break }
    }
    for down in [true, false] {
        let ev = CGEvent(keyboardEventSource: nil, virtualKey: code, keyDown: down)!
        ev.flags = flags
        ev.postToPid(pid)
    }
    print("key \(code)")
case "hidkey":
    // Like a real keyboard: through the HID event tap, with modifier
    // flagsChanged events and short gaps. Refuses unless the target app is
    // frontmost, so it can never type into another app.
    guard NSWorkspace.shared.frontmostApplication?.processIdentifier == pid else {
        print("hidkey refused: target not frontmost"); exit(3)
    }
    let code = CGKeyCode(args[3])!
    var flags: CGEventFlags = []
    var modifierCodes: [CGKeyCode] = []
    for m in args.dropFirst(4) {
        switch m {
        case "cmd": flags.insert(.maskCommand); modifierCodes.append(55)
        case "shift": flags.insert(.maskShift); modifierCodes.append(56)
        case "opt": flags.insert(.maskAlternate); modifierCodes.append(58)
        case "ctrl": flags.insert(.maskControl); modifierCodes.append(59)
        default: break
        }
    }
    let source = CGEventSource(stateID: .hidSystemState)
    func post(_ key: CGKeyCode, _ down: Bool, _ eventFlags: CGEventFlags) {
        let ev = CGEvent(keyboardEventSource: source, virtualKey: key, keyDown: down)!
        ev.flags = eventFlags
        ev.post(tap: .cghidEventTap)
        usleep(30000)
    }
    for m in modifierCodes { post(m, true, flags) }
    post(code, true, flags)
    post(code, false, flags)
    for m in modifierCodes.reversed() { post(m, false, []) }
    print("hidkey \(code)")
case "focus":
    let needle = args[3]
    var found: AXUIElement?
    _ = walk(app, 0, 30) { e, _ in
        if matches(e, needle) { found = e; return true }
        return false
    }
    guard let f = found else { print("NOT FOUND"); exit(1) }
    let r = AXUIElementSetAttributeValue(f, kAXFocusedAttribute as CFString, kCFBooleanTrue)
    print("focus \(label(f)) -> \(r.rawValue)")
case "setvalue":
    let needle = args[3]
    var found: AXUIElement?
    _ = walk(app, 0, 30) { e, _ in
        let l = [str(e, kAXTitleAttribute), str(e, kAXDescriptionAttribute), str(e, "AXIdentifier")]
        if str(e, kAXRoleAttribute) == "AXTextField", l.contains(where: { $0 == needle }) { found = e; return true }
        return false
    }
    guard let f = found else { print("NOT FOUND"); exit(1) }
    let r = AXUIElementSetAttributeValue(f, kAXValueAttribute as CFString, args[4] as CFString)
    print("setvalue \(label(f)) -> \(r.rawValue) now=\(str(f, kAXValueAttribute))")
case "enabled":
    // Enabled state of every element whose title/description equals the name
    // (for example a menu item, whose key equivalent works only if enabled).
    let needle = args[3]
    _ = walk(app, 0, 30) { e, _ in
        let l = [str(e, kAXTitleAttribute), str(e, kAXDescriptionAttribute)]
        if l.contains(where: { $0 == needle }) {
            let enabled = (attr(e, kAXEnabledAttribute) as? Bool).map { String($0) } ?? "?"
            print("\(label(e)) enabled=\(enabled)")
        }
        return false
    }
case "focused":
    // Which element and windows receive keyboard input right now.
    func element(_ attribute: String) -> AXUIElement? {
        guard let value = attr(app, attribute) else { return nil }
        return (value as! AXUIElement)
    }
    let front = NSWorkspace.shared.frontmostApplication
    print("frontmostApp: \(front?.localizedName ?? "none") pid=\(front?.processIdentifier ?? -1) target=\(pid)")
    for (name, attribute) in [("focusedElement", kAXFocusedUIElementAttribute),
                              ("focusedWindow", kAXFocusedWindowAttribute),
                              ("mainWindow", kAXMainWindowAttribute)] {
        if let e = element(attribute) {
            print("\(name): \(label(e))")
        } else {
            print("\(name): none")
        }
    }
case "activate":
    let r = AXUIElementSetAttributeValue(app, kAXFrontmostAttribute as CFString, kCFBooleanTrue)
    print("frontmost -> \(r.rawValue)")
    // Cooperative activation (macOS 14+) can accept the AX request without
    // bringing the app forward while another app is frontmost (build 45
    // journeys lost to Terminal Cockpit). An Apple Event activate by bundle
    // id still works; it sends no reopen event, so no window is created.
    Thread.sleep(forTimeInterval: 0.3)
    if NSWorkspace.shared.frontmostApplication?.processIdentifier != pid,
       let bundle = NSRunningApplication(processIdentifier: pid)?.bundleIdentifier {
        var error: NSDictionary?
        NSAppleScript(source: "tell application id \"\(bundle)\" to activate")?
            .executeAndReturnError(&error)
        Thread.sleep(forTimeInterval: 0.5)
        let front = NSWorkspace.shared.frontmostApplication?.processIdentifier == pid
        print("activate via Apple Event -> \(front ? "frontmost" : "still behind")")
    }
case "hidrightclick":
    // Like a real mouse: through the HID event tap (Chromium views ignore
    // mouse events posted to the process). Refuses unless the target app is
    // frontmost, so it can never click into another app.
    guard NSWorkspace.shared.frontmostApplication?.processIdentifier == pid else {
        print("hidrightclick refused: target not frontmost"); exit(3)
    }
    var hfound: AXUIElement?
    _ = walk(app, 0, 30) { e, _ in
        if matches(e, args[3]) { hfound = e; return true }
        return false
    }
    guard let hf = hfound, let hpv = attr(hf, kAXPositionAttribute), let hsv = attr(hf, kAXSizeAttribute) else {
        print("NOT FOUND"); exit(1)
    }
    var hpos = CGPoint.zero, hsize = CGSize.zero
    AXValueGetValue(hpv as! AXValue, .cgPoint, &hpos)
    AXValueGetValue(hsv as! AXValue, .cgSize, &hsize)
    let hc = CGPoint(x: hpos.x + hsize.width / 2, y: hpos.y + hsize.height / 2)
    for t: CGEventType in [.mouseMoved, .rightMouseDown, .rightMouseUp] {
        let ev = CGEvent(mouseEventSource: nil, mouseType: t, mouseCursorPosition: hc,
                         mouseButton: .right)!
        // A click count of 0 is not a click for AppKit.
        if t != .mouseMoved { ev.setIntegerValueField(.mouseEventClickState, value: 1) }
        ev.post(tap: .cghidEventTap)
        usleep(80000)
    }
    print("hidrightclicked \(label(hf)) at \(hc)")
case "hidscroll":
    // A pixel scroll wheel event through the HID tap at the element's center,
    // optionally with modifiers (Cmd+scroll switches tabs, NAV-11). Refuses
    // unless the target app is frontmost.
    guard NSWorkspace.shared.frontmostApplication?.processIdentifier == pid else {
        print("hidscroll refused: target not frontmost"); exit(3)
    }
    guard args.count >= 5, let c = centerOf(app, args[3]), let dy = Int32(args[4]) else {
        print("NOT FOUND"); exit(1)
    }
    let dx = args.count >= 6 ? Int32(args[5]) ?? 0 : 0
    let mods = flagsFrom(args.dropFirst(args.count >= 6 && Int32(args[5]) != nil ? 6 : 5))
    CGEvent(mouseEventSource: nil, mouseType: .mouseMoved, mouseCursorPosition: c,
            mouseButton: .left)!.post(tap: .cghidEventTap)
    usleep(80000)
    let ev = CGEvent(scrollWheelEvent2Source: nil, units: .pixel, wheelCount: 2,
                     wheel1: dy, wheel2: dx, wheel3: 0)!
    ev.flags = mods
    ev.location = c
    ev.post(tap: .cghidEventTap)
    print("hidscrolled dy=\(dy) dx=\(dx) at \(c)")
case "hidmiddle":
    // Middle click at the element's center, then the pointer moves by
    // (dx, dy) in small steps and stays there (middle-click autoscroll,
    // NAV-12). Refuses unless the target app is frontmost.
    guard NSWorkspace.shared.frontmostApplication?.processIdentifier == pid else {
        print("hidmiddle refused: target not frontmost"); exit(3)
    }
    guard args.count >= 6, let c = centerOf(app, args[3]),
          let mdx = Double(args[4]), let mdy = Double(args[5]) else {
        print("NOT FOUND"); exit(1)
    }
    CGEvent(mouseEventSource: nil, mouseType: .mouseMoved, mouseCursorPosition: c,
            mouseButton: .left)!.post(tap: .cghidEventTap)
    usleep(80000)
    for t: CGEventType in [.otherMouseDown, .otherMouseUp] {
        let ev = CGEvent(mouseEventSource: nil, mouseType: t, mouseCursorPosition: c,
                         mouseButton: .center)!
        ev.setIntegerValueField(.mouseEventClickState, value: 1)
        ev.post(tap: .cghidEventTap)
        usleep(80000)
    }
    for i in 1...10 {
        let p = CGPoint(x: c.x + mdx * Double(i) / 10, y: c.y + mdy * Double(i) / 10)
        CGEvent(mouseEventSource: nil, mouseType: .mouseMoved, mouseCursorPosition: p,
                mouseButton: .left)!.post(tap: .cghidEventTap)
        usleep(30000)
    }
    print("hidmiddle at \(c) moved by (\(mdx), \(mdy))")
case "click", "rightclick":
    let needle = args[3]
    var found: AXUIElement?
    _ = walk(app, 0, 30) { e, _ in
        if matches(e, needle) { found = e; return true }
        return false
    }
    guard let f = found, let pv = attr(f, kAXPositionAttribute), let sv = attr(f, kAXSizeAttribute) else {
        print("NOT FOUND"); exit(1)
    }
    var pos = CGPoint.zero, size = CGSize.zero
    AXValueGetValue(pv as! AXValue, .cgPoint, &pos)
    AXValueGetValue(sv as! AXValue, .cgSize, &size)
    let c = CGPoint(x: pos.x + size.width / 2, y: pos.y + size.height / 2)
    let right = args[1] == "rightclick"
    let types: [CGEventType] = right ? [.rightMouseDown, .rightMouseUp] : [.leftMouseDown, .leftMouseUp]
    for t in types {
        let ev = CGEvent(mouseEventSource: nil, mouseType: t, mouseCursorPosition: c,
                         mouseButton: right ? .right : .left)!
        ev.postToPid(pid)
        usleep(60000)
    }
    print("clicked \(label(f)) at \(c)")
default:
    print("unknown command"); exit(2)
}
