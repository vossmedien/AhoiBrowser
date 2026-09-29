// Minimal AppKit app for WORKFLOW-03 diagnostics: logs every keyDown and
// flagsChanged it receives (window-level and application-level) to stdout, so
// a probe can tell whether macOS delivers a key such as Option+Tab to a
// frontmost app at all. No files, no network.
import AppKit

final class KeyLogWindow: NSWindow {
  override func sendEvent(_ event: NSEvent) {
    if event.type == .keyDown || event.type == .flagsChanged {
      print("window \(event.type == .keyDown ? "keyDown" : "flags") code=\(event.keyCode) flags=\(event.modifierFlags.rawValue)")
      fflush(stdout)
    }
    super.sendEvent(event)
  }
}

final class KeyLogApp: NSApplication {
  override func sendEvent(_ event: NSEvent) {
    if event.type == .keyDown {
      print("app keyDown code=\(event.keyCode) flags=\(event.modifierFlags.rawValue)")
      fflush(stdout)
    }
    super.sendEvent(event)
  }
}

let app = KeyLogApp.shared
app.setActivationPolicy(.regular)
let window = KeyLogWindow(contentRect: NSRect(x: 200, y: 200, width: 320, height: 120),
                          styleMask: [.titled], backing: .buffered, defer: false)
window.title = "Ahoi key log"
window.makeKeyAndOrderFront(nil)
app.activate(ignoringOtherApps: true)
print("ready pid=\(ProcessInfo.processInfo.processIdentifier)")
fflush(stdout)
app.run()
