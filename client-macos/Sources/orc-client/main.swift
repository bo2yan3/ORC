// orc-client: connects to an orc-host and shows its screen in a window.
//
//   swift run -c release orc-client <host> <token> [--port N] [--scroll-scale X] [--invert-scroll]
import AppKit
import CoreMedia

struct Options {
    var host = ""
    var token = ""
    var port = Proto.defaultPort
    var scrollScale: CGFloat = 4
    var invertScroll = false
}

func parseOptions() -> Options? {
    var o = Options()
    var positional: [String] = []
    var args = Array(CommandLine.arguments.dropFirst()).makeIterator()
    while let a = args.next() {
        switch a {
        case "--port": guard let v = args.next(), let p = UInt16(v) else { return nil }; o.port = p
        case "--scroll-scale": guard let v = args.next(), let s = Double(v) else { return nil }; o.scrollScale = s
        case "--invert-scroll": o.invertScroll = true
        default: positional.append(a)
        }
    }
    guard positional.count == 2 else { return nil }
    o.host = positional[0]
    o.token = positional[1]
    return o
}

final class AppDelegate: NSObject, NSApplicationDelegate, NSWindowDelegate {
    let opts: Options
    var window: NSWindow!
    var view: StreamView!
    var conn: Connection!
    let depacketizer = HEVCDepacketizer()

    // stats for the title bar
    var framesThisSecond = 0
    var bytesThisSecond = 0
    var rttMs: Double?
    var lastKeyframeRequest = Date.distantPast

    init(opts: Options) { self.opts = opts }

    func applicationDidFinishLaunching(_ notification: Notification) {
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 1440, height: 900),
                          styleMask: [.titled, .closable, .resizable, .miniaturizable],
                          backing: .buffered, defer: false)
        window.collectionBehavior = [.fullScreenPrimary]
        window.title = "ORC — connecting to \(opts.host)…"
        window.delegate = self
        window.center()

        view = StreamView(frame: window.contentView!.bounds)
        view.autoresizingMask = [.width, .height]
        view.preciseScrollScale = opts.scrollScale
        view.invertScroll = opts.invertScroll
        window.contentView = view
        window.makeKeyAndOrderFront(nil)
        window.makeFirstResponder(view)
        NSApp.activate(ignoringOtherApps: true)

        // Route all keyboard events to the stream while our window is focused.
        NSEvent.addLocalMonitorForEvents(matching: [.keyDown, .keyUp, .flagsChanged]) { [weak self] event in
            guard let self, event.window === self.window, self.window.isKeyWindow else { return event }
            return self.view.handleKeyEvent(event) ? nil : event
        }

        conn = Connection(host: opts.host, port: opts.port)
        view.connection = conn
        conn.onMessage = { [weak self] in self?.handle($0) }
        conn.onClosed = { reason in
            print("disconnected: \(reason)")
            NSApp.terminate(nil)
        }
        conn.start(token: opts.token)

        Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in self?.tick() }
    }

    func handle(_ m: Message) {
        switch MsgType(rawValue: m.type) {
        case .helloAck:
            guard m.payload.count >= 6 else { return }
            let status = m.payload[m.payload.startIndex]
            guard status == 0 else {
                conn.close(reason: status == 1 ? "wrong token" : "protocol version mismatch")
                return
            }
            let w = m.payload.readU16(1), h = m.payload.readU16(3)
            view.remoteSize = CGSize(width: Int(w), height: Int(h))
            print("connected: remote screen \(w)x\(h)")
        case .video:
            guard m.payload.count > 8 else { return }
            framesThisSecond += 1
            bytesThisSecond += m.payload.count
            let au = m.payload.subdata(in: (m.payload.startIndex + 8)..<m.payload.endIndex)
            if let sample = depacketizer.sampleBuffer(fromAccessUnit: au) {
                if let fmt = depacketizer.format {
                    let d = CMVideoFormatDescriptionGetDimensions(fmt)
                    let size = CGSize(width: Int(d.width), height: Int(d.height))
                    if size != view.remoteSize { view.remoteSize = size }
                }
                if !view.enqueue(sample) { requestKeyframe() }
            } else if depacketizer.format == nil {
                requestKeyframe()  // joined mid-stream without parameter sets
            }
        case .ping:
            conn.send(.pong, m.payload)
        case .pong:
            guard m.payload.count >= 8 else { return }
            let sent = m.payload.readU64(0)
            rttMs = Double(nowMicros() &- sent) / 1000
        default:
            break  // ignore unknown types
        }
    }

    func requestKeyframe() {
        guard Date().timeIntervalSince(lastKeyframeRequest) > 0.5 else { return }
        lastKeyframeRequest = Date()
        conn.send(.keyframeReq)
    }

    func tick() {
        var p = Data()
        p.appendBE(nowMicros())
        conn.send(.ping, p)

        let mbps = Double(bytesThisSecond) * 8 / 1_000_000
        let rtt = rttMs.map { String(format: "%.0f ms", $0) } ?? "–"
        window.title = String(format: "ORC — %@ · %d fps · %.1f Mbps · RTT %@",
                              opts.host, framesThisSecond, mbps, rtt)
        framesThisSecond = 0
        bytesThisSecond = 0
    }

    func nowMicros() -> UInt64 { UInt64(DispatchTime.now().uptimeNanoseconds / 1000) }

    func windowDidResignKey(_ notification: Notification) { view.releaseAll() }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
}

guard let opts = parseOptions() else {
    print("usage: orc-client <host> <token> [--port N] [--scroll-scale X] [--invert-scroll]")
    exit(2)
}

let app = NSApplication.shared
app.setActivationPolicy(.regular)
let delegate = AppDelegate(opts: opts)
app.delegate = delegate
app.run()
