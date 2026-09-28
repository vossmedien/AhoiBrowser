import Foundation
let url = URL(fileURLWithPath: CommandLine.arguments[1])
let want = try JSONSerialization.jsonObject(with: Data(contentsOf: url)) as! [[String: Any]]
let got = SyncConformanceFieldCatalogue.entries
precondition(got.count == want.count, "count")
for (e, w) in zip(got, want) {
  precondition(e.entityType == w["id"] as! Int && e.dataClass == w["dataClass"] as! String
    && e.wireModelVersion == w["wireModelVersion"] as! Int && e.fieldGroups == w["fieldGroups"] as! [String], "entry \(e.entityType)")
}
print("swift catalogue matches \(got.count) entries")
