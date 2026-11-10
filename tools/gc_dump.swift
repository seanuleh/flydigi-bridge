import Foundation
import GameController
// Prints the state of the first extended gamepad whenever it changes:  G <t> name=value ...
setvbuf(stdout, nil, _IOLBF, 0)
let t0 = Date()
let dur = Double(CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "10")!
GCController.shouldMonitorBackgroundEvents = true
func dump(_ g: GCExtendedGamepad) {
    var parts: [String] = []
    for (name, el) in g.elements.sorted(by: { $0.key < $1.key }) {
        if let b = el as? GCControllerButtonInput { if b.value > 0.01 { parts.append("\(name)=\(String(format: "%.2f", b.value))") } }
        else if let d = el as? GCControllerDirectionPad {
            if abs(d.xAxis.value) > 0.05 || abs(d.yAxis.value) > 0.05 { parts.append("\(name)=\(String(format: "%.2f,%.2f", d.xAxis.value, d.yAxis.value))") }
        }
    }
    print(String(format: "G %.4f ", Date().timeIntervalSince(t0)) + parts.joined(separator: " "))
}
var attached = Set<ObjectIdentifier>()
func attach() {
    for c in GCController.controllers() {
        if attached.contains(ObjectIdentifier(c)) { continue }
        attached.insert(ObjectIdentifier(c))
        print("# controller: \(c.vendorName ?? "?") \(c.productCategory)")
        c.extendedGamepad?.valueChangedHandler = { g, _ in dump(g) }
    }
}
NotificationCenter.default.addObserver(forName: .GCControllerDidConnect, object: nil, queue: .main) { _ in attach() }
attach()
DispatchQueue.main.asyncAfter(deadline: .now() + dur) { exit(0) }
RunLoop.main.run()
