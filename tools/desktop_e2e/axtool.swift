import AppKit
// PID-scoped Accessibility helper for isolated Ahoi E2E clones.
// usage: axtool dump <pid> [depth]
//        axtool enable <pid>
//        axtool press <pid> <substring-of-title|description|identifier>
//        axtool pressin <pid> <parent> <child> [action]
//        axtool type <pid> <text>
//        axtool key <pid> <virtualKeyCode> [cmd|shift|opt|ctrl ...]
//        axtool hidscroll <pid> <element> <dy> [dx] [phased|line] [cmd|shift|opt|ctrl ...]
//        axtool hidmiddle <pid> <element> <dx> <dy>
//        axtool hidclick <pid> <label substring> [cmd|shift|opt|ctrl ...]
//        axtool selected <pid>
//        axtool menukeys <pid>
//        axtool eventaccess <pid>
//        axtool hidready <pid>
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

// Read the system's keyboard receiver and the mouse hit target, not just
// NSWorkspace's frontmost app or a successful AXSetFrontmost request.
func hidTargetReady(_ pid: pid_t, at point: CGPoint? = nil) -> Bool {
    func copyFocus(_ element: AXUIElement, _ name: String, scope: String) -> AXUIElement? {
        var value: CFTypeRef?
        let status = AXUIElementCopyAttributeValue(element, name as CFString, &value)
        let valueType = value.map { String(CFGetTypeID($0)) } ?? "none"
        print("AXCopy \(scope) \(name) status=\(status.rawValue) valueType=\(valueType) AXUIElementType=\(AXUIElementGetTypeID())")
        guard status == .success, let value,
              CFGetTypeID(value) == AXUIElementGetTypeID() else { return nil }
        return value as! AXUIElement
    }
    let system = AXUIElementCreateSystemWide()
    AXUIElementSetMessagingTimeout(system, 1.5)
    var receiverPID: pid_t = -1, windowPID: pid_t = -1, hitPID: pid_t = -1
    var receiverWindow = "none"
    if let receiver = copyFocus(system, kAXFocusedApplicationAttribute, scope: "system") {
        AXUIElementSetMessagingTimeout(receiver, 1.5)
        let receiverStatus = AXUIElementGetPid(receiver, &receiverPID)
        print("AXPid focusedApplication status=\(receiverStatus.rawValue) pid=\(receiverPID)")
        if let window = copyFocus(receiver, kAXFocusedWindowAttribute, scope: "focusedApplication") {
            let windowStatus = AXUIElementGetPid(window, &windowPID)
            print("AXPid focusedWindow status=\(windowStatus.rawValue) pid=\(windowPID)")
            receiverWindow = label(window)
        }
    }
    if receiverPID == -1 || windowPID == -1 {
        var attributes: CFArray?
        let status = AXUIElementCopyAttributeNames(system, &attributes)
        let names = (attributes as? [String])?.sorted().joined(separator: ",") ?? "unavailable"
        print("AXAttributeNames system status=\(status.rawValue) names=[\(names)]")
        // Diagnose another public system focus attribute; it never admits HID.
        if let focused = copyFocus(system, kAXFocusedUIElementAttribute, scope: "system") {
            var focusedPID: pid_t = -1
            let focusedStatus = AXUIElementGetPid(focused, &focusedPID)
            print("AXPid systemFocusedUI status=\(focusedStatus.rawValue) pid=\(focusedPID) element=\(label(focused))")
            if let window = copyFocus(focused, kAXWindowAttribute, scope: "systemFocusedUI") {
                var focusedWindowPID: pid_t = -1
                let windowStatus = AXUIElementGetPid(window, &focusedWindowPID)
                print("AXPid systemFocusedUIWindow status=\(windowStatus.rawValue) pid=\(focusedWindowPID) window=\(label(window))")
            }
        }
    }
    if let point {
        var hit: AXUIElement?
        let status = AXUIElementCopyElementAtPosition(system, Float(point.x), Float(point.y), &hit)
        print("AXHitTest status=\(status.rawValue)")
        if status == .success, let hit {
            let hitStatus = AXUIElementGetPid(hit, &hitPID)
            print("AXPid mouseReceiver status=\(hitStatus.rawValue) pid=\(hitPID)")
            print("mouseReceiver: \(label(hit)) pid=\(hitPID)")
        }
    }
    let frontPID = NSWorkspace.shared.frontmostApplication?.processIdentifier ?? -1
    let postAccess = CGPreflightPostEventAccess()
    let windows = CGWindowListCopyWindowInfo([.optionOnScreenOnly, .excludeDesktopElements],
                                            kCGNullWindowID) as? [[String: Any]]
    let systemModalPIDs = windows?.filter {
        $0[kCGWindowOwnerName as String] as? String == "UserNotificationCenter" &&
        ($0[kCGWindowAlpha as String] as? Double ?? 1) > 0
    }.compactMap { $0[kCGWindowOwnerPID as String] as? Int } ?? []
    let mouseReceiver = point == nil ? "not-probed" : String(hitPID)
    print("HID target=\(pid) workspace=\(frontPID) keyboardReceiver=\(receiverPID) windowOwner=\(windowPID) mouseReceiver=\(mouseReceiver) PostEventAccess=\(postAccess)")
    print("receiverWindow: \(receiverWindow)")
    print("QuartzGUI=\(windows != nil) UserNotificationCenterWindows=\(systemModalPIDs)")
    return postAccess && frontPID == pid && receiverPID == pid && windowPID == pid &&
        (point == nil || hitPID == pid) && windows != nil && systemModalPIDs.isEmpty
}

let args = CommandLine.arguments
guard args.count >= 3, let pid = pid_t(args[2]) else {
    FileHandle.standardError.write("bad args\n".data(using: .utf8)!); exit(2)
}
// Observe this helper's access without requesting permission or posting events.
if args[1] == "eventaccess" {
    let trusted = AXIsProcessTrusted()
    let postAccess = CGPreflightPostEventAccess()
    print("AXTrusted=\(trusted) PostEventAccess=\(postAccess)")
    exit(trusted && postAccess ? 0 : 3)
}
guard AXIsProcessTrusted() else {
    FileHandle.standardError.write("not AX trusted\n".data(using: .utf8)!); exit(3)
}
let app = AXUIElementCreateApplication(pid)
switch args[1] {
case "hidready":
    exit(hidTargetReady(pid) ? 0 : 3)
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
case "pressin":
    // Presses <child> only inside the first <parent> subtree: a submenu item
    // whose title another item of the same menu also has (the Workspace
    // menu's "Zusammenführen mit" targets repeat the switcher's names).
    guard args.count >= 5 else { print("bad args"); exit(2) }
    var parent: AXUIElement?
    _ = walk(app, 0, 45) { e, _ in
        if matches(e, args[3]) { parent = e; return true }
        return false
    }
    guard let p = parent else { print("PARENT NOT FOUND"); exit(1) }
    var found: AXUIElement?
    _ = walk(p, 0, 12) { e, d in
        if d > 0, matches(e, args[4]) { found = e; return true }
        return false
    }
    guard let f = found else { print("NOT FOUND in \(label(p))"); exit(1) }
    let action = args.count > 5 ? args[5] : kAXPressAction
    AXUIElementSetMessagingTimeout(f, 1.5)
    print("\(action) \(label(f)) in \(label(p)) -> \(AXUIElementPerformAction(f, action as CFString).rawValue)")
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
    // flagsChanged events and short gaps. Check the target before each
    // key-down; focus can change while the modifier chord is assembled.
    guard hidTargetReady(pid) else {
        print("hidkey refused: input target not ready"); exit(3)
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
    var pressedModifiers: [CGKeyCode] = []
    func checkOwnerBeforeDown() {
        guard hidTargetReady(pid) else {
            // Release only this invocation's already posted modifier downs.
            // No character/action key is sent after ownership is lost.
            for m in pressedModifiers.reversed() { post(m, false, []) }
            print("hidkey cancelled: focus changed during chord")
            exit(8)
        }
    }
    for m in modifierCodes {
        checkOwnerBeforeDown()
        post(m, true, flags)
        pressedModifiers.append(m)
    }
    checkOwnerBeforeDown()
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
case "menukeys":
    // Key equivalents of the menu items: "title | key | modifiers", where
    // AXMenuItemCmdModifiers is 0 for ⌘ alone (+1 ⇧, +2 ⌥, +4 ⌃, +8 no ⌘).
    _ = walk(app, 0, 8) { e, _ in
        if str(e, kAXRoleAttribute) == "AXMenuItem" {
            let key = str(e, "AXMenuItemCmdChar")
            if !key.isEmpty {
                print("\(str(e, kAXTitleAttribute)) | \(key) | \(str(e, "AXMenuItemCmdModifiers"))")
            }
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
    _ = hidTargetReady(pid)
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
    guard hidTargetReady(pid, at: hc) else {
        print("hidrightclick refused: input target not ready"); exit(3)
    }
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
    var rest = Array(args.dropFirst(5))
    var dx: Int32 = 0
    if let first = rest.first, let v = Int32(first) { dx = v; rest.removeFirst() }
    // "phased": a trackpad-like stream (Began, Changed x3, Ended) in pixels;
    // "line": one classic mouse-wheel notch in lines; default: one phase-less
    // pixel event.
    var mode = "pixel"
    if let first = rest.first, first == "phased" || first == "line" { mode = first; rest.removeFirst() }
    let mods = flagsFrom(rest[...])
    guard hidTargetReady(pid, at: c) else {
        print("hidscroll refused: input target not ready"); exit(3)
    }
    CGEvent(mouseEventSource: nil, mouseType: .mouseMoved, mouseCursorPosition: c,
            mouseButton: .left)!.post(tap: .cghidEventTap)
    usleep(80000)
    func post(_ units: CGScrollEventUnit, _ y: Int32, _ x: Int32, phase: Int64) {
        let ev = CGEvent(scrollWheelEvent2Source: nil, units: units, wheelCount: 2,
                         wheel1: y, wheel2: x, wheel3: 0)!
        ev.flags = mods
        ev.location = c
        if phase != 0 { ev.setIntegerValueField(.scrollWheelEventScrollPhase, value: phase) }
        ev.post(tap: .cghidEventTap)
        usleep(16000)
    }
    switch mode {
    case "phased":
        // NSEventPhase values: began 1, changed 4, ended 8.
        post(.pixel, 0, 0, phase: 1)
        for _ in 0..<3 { post(.pixel, dy / 3, dx / 3, phase: 4) }
        post(.pixel, 0, 0, phase: 8)
    case "line":
        post(.line, dy > 0 ? 1 : -1, 0, phase: 0)
    default:
        post(.pixel, dy, dx, phase: 0)
    }
    print("hidscrolled \(mode) dy=\(dy) dx=\(dx) at \(c)")
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
    guard hidTargetReady(pid, at: c) else {
        print("hidmiddle refused: input target not ready"); exit(3)
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
case "hidclick":
    // Left click through the HID tap at the center of the first element whose
    // AX label contains the substring (list entries in web UI, which have no
    // pressable title). Refuses unless the target app is frontmost.
    guard NSWorkspace.shared.frontmostApplication?.processIdentifier == pid else {
        print("hidclick refused: target not frontmost"); exit(3)
    }
    var cfound: AXUIElement?
    _ = walk(app, 0, 40) { e, _ in
        if label(e).contains(args[3]) && attr(e, kAXPositionAttribute) != nil {
            cfound = e; return true
        }
        return false
    }
    guard let cf = cfound, let cpv = attr(cf, kAXPositionAttribute),
          let csv = attr(cf, kAXSizeAttribute) else { print("NOT FOUND"); exit(1) }
    var cpos = CGPoint.zero, csize = CGSize.zero
    AXValueGetValue(cpv as! AXValue, .cgPoint, &cpos)
    AXValueGetValue(csv as! AXValue, .cgSize, &csize)
    let cc = CGPoint(x: cpos.x + csize.width / 2, y: cpos.y + csize.height / 2)
    guard hidTargetReady(pid, at: cc) else {
        print("hidclick refused: input target not ready"); exit(3)
    }
    // Optional modifiers ride on the mouse events (⌘/⇧-click selection).
    let cflags = flagsFrom(args.dropFirst(4))
    for t: CGEventType in [.mouseMoved, .leftMouseDown, .leftMouseUp] {
        let ev = CGEvent(mouseEventSource: nil, mouseType: t, mouseCursorPosition: cc,
                         mouseButton: .left)!
        if t != .mouseMoved { ev.setIntegerValueField(.mouseEventClickState, value: 1) }
        if !cflags.isEmpty { ev.flags = cflags }
        ev.post(tap: .cghidEventTap)
        usleep(80000)
    }
    print("hidclicked \(label(cf)) at \(cc)")
case "selected":
    // One line per element whose AXSelected is true (sidebar rows).
    _ = walk(app, 0, 40) { e, _ in
        if (attr(e, kAXSelectedAttribute) as? Bool) == true { print(label(e)) }
        return false
    }
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
