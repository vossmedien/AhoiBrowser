// H3 command-bar driver helper: types into a text field WITHOUT HID events.
// Setting kAXSelectedTextAttribute maps to ui::AXActionData
// kReplaceSelectedText (ax_platform_node_cocoa.mm), which views::Textfield
// handles as InsertOrReplaceText with TextChangeType::kUserTriggered, so the
// controller's ContentsChanged (and Ahoi.CommandBar.RebuildSuggestions) runs
// once per inserted character, like a keystroke. Build before a leased run:
//   xcrun swiftc -O -o /private/tmp/ahoi-ax-insert tools/perf/drivers/ax_insert_text.swift
// usage: ahoi-ax-insert <pid> <text field title/description> <text> [delay-ms]
import ApplicationServices
import Foundation

let args = CommandLine.arguments
guard args.count >= 4, let pid = pid_t(args[1]) else {
    FileHandle.standardError.write("usage: ahoi-ax-insert <pid> <field> <text> [delay-ms]\n".data(using: .utf8)!)
    exit(2)
}
let needle = args[2]
let delay = args.count > 4 ? (UInt32(args[4]) ?? 500) : 500

func string(_ element: AXUIElement, _ name: String) -> String {
    var value: AnyObject?
    AXUIElementCopyAttributeValue(element, name as CFString, &value)
    return value as? String ?? ""
}

func find(_ element: AXUIElement, depth: Int) -> AXUIElement? {
    if string(element, kAXRoleAttribute) == "AXTextField",
       [string(element, kAXTitleAttribute), string(element, kAXDescriptionAttribute)].contains(needle) {
        return element
    }
    guard depth < 30 else { return nil }
    var children: AnyObject?
    AXUIElementCopyAttributeValue(element, kAXChildrenAttribute as CFString, &children)
    for child in (children as? [AXUIElement]) ?? [] {
        if let found = find(child, depth: depth + 1) { return found }
    }
    return nil
}

guard let field = find(AXUIElementCreateApplication(pid), depth: 0) else {
    print("NOT FOUND"); exit(1)
}
AXUIElementSetMessagingTimeout(field, 1.5)
for character in args[3] {
    let result = AXUIElementSetAttributeValue(
        field, kAXSelectedTextAttribute as CFString, String(character) as CFString)
    guard result == .success else { print("insert failed \(result.rawValue)"); exit(3) }
    usleep(delay * 1000)
}
print("inserted \(args[3].count) now=\(string(field, kAXValueAttribute))")
