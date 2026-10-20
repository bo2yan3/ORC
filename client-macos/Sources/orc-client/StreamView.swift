import AppKit
import AVFoundation

/// Shows the remote screen and forwards mouse/keyboard input to the host.
final class StreamView: NSView {
    weak var connection: Connection?
    /// Remote screen size, used to work out where the letterboxed video sits.
    var remoteSize = CGSize(width: 16, height: 9) { didSet { updateVideoRect() } }

    /// Trackpad pixels -> Windows wheel units (120 = one notch). Tune to taste.
    var preciseScrollScale: CGFloat = 4
    var invertScroll = false

    private let displayLayer = AVSampleBufferDisplayLayer()
    private var videoRect = CGRect.zero
    private var heldModifiers = Set<UInt16>()
    private var scrollRemainder = CGPoint.zero
    private var trackingArea: NSTrackingArea?

    override init(frame: NSRect) {
        super.init(frame: frame)
        wantsLayer = true
        layer = CALayer()
        layer?.backgroundColor = NSColor.black.cgColor
        displayLayer.videoGravity = .resizeAspect
        displayLayer.frame = bounds
        layer?.addSublayer(displayLayer)
    }

    required init?(coder: NSCoder) { fatalError("not used") }

    override var acceptsFirstResponder: Bool { true }
    override var isFlipped: Bool { true }  // y grows downward, like Windows

    override func layout() {
        super.layout()
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        displayLayer.frame = bounds
        CATransaction.commit()
        updateVideoRect()
    }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        if let trackingArea { removeTrackingArea(trackingArea) }
        let area = NSTrackingArea(rect: bounds,
                                  options: [.mouseMoved, .activeInKeyWindow, .inVisibleRect, .cursorUpdate],
                                  owner: self, userInfo: nil)
        addTrackingArea(area)
        trackingArea = area
    }

    // MARK: Video

    /// Returns false if the decoder failed and needs a keyframe.
    @discardableResult
    func enqueue(_ sample: CMSampleBuffer) -> Bool {
        let renderer = displayLayer.sampleBufferRenderer
        if renderer.status == .failed {
            print("decoder failed: \(String(describing: renderer.error)); flushing")
            renderer.flush(removingDisplayedImage: false, completionHandler: nil)
            return false
        }
        renderer.enqueue(sample)
        return true
    }

    private func updateVideoRect() {
        videoRect = AVMakeRect(aspectRatio: remoteSize, insideRect: bounds)
    }

    // MARK: Mouse

    private func sendMouseMove(_ event: NSEvent) {
        guard videoRect.width > 0, videoRect.height > 0 else { return }
        let p = convert(event.locationInWindow, from: nil)
        let fx = min(max((p.x - videoRect.minX) / videoRect.width, 0), 1)
        let fy = min(max((p.y - videoRect.minY) / videoRect.height, 0), 1)
        var d = Data()
        d.appendBE(UInt16(fx * 65535))
        d.appendBE(UInt16(fy * 65535))
        connection?.send(.mouseMove, d)
    }

    private func sendButton(_ button: UInt8, down: Bool, _ event: NSEvent) {
        sendMouseMove(event)  // make sure the click lands where the cursor is
        connection?.send(.mouseButton, Data([button, down ? 1 : 0]))
    }

    override func mouseMoved(with event: NSEvent) { sendMouseMove(event) }
    override func mouseDragged(with event: NSEvent) { sendMouseMove(event) }
    override func rightMouseDragged(with event: NSEvent) { sendMouseMove(event) }
    override func otherMouseDragged(with event: NSEvent) { sendMouseMove(event) }

    override func mouseDown(with event: NSEvent) {
        window?.makeFirstResponder(self)
        sendButton(0, down: true, event)
    }
    override func mouseUp(with event: NSEvent) { sendButton(0, down: false, event) }
    override func rightMouseDown(with event: NSEvent) { sendButton(1, down: true, event) }
    override func rightMouseUp(with event: NSEvent) { sendButton(1, down: false, event) }

    // buttonNumber: 2 = middle, 3 = back, 4 = forward
    override func otherMouseDown(with event: NSEvent) {
        if let b = otherButton(event) { sendButton(b, down: true, event) }
    }
    override func otherMouseUp(with event: NSEvent) {
        if let b = otherButton(event) { sendButton(b, down: false, event) }
    }
    private func otherButton(_ e: NSEvent) -> UInt8? {
        switch e.buttonNumber {
        case 2: return 2
        case 3: return 3
        case 4: return 4
        default: return nil
        }
    }

    override func scrollWheel(with event: NSEvent) {
        var dx = event.scrollingDeltaX
        var dy = event.scrollingDeltaY
        if event.hasPreciseScrollingDeltas {  // trackpad / Magic Mouse: pixels
            dx *= preciseScrollScale
            dy *= preciseScrollScale
        } else {                              // wheel mouse: lines
            dx *= 120
            dy *= 120
        }
        if invertScroll { dx = -dx; dy = -dy }
        // Windows: positive horizontal wheel scrolls right; macOS deltaX is the opposite.
        dx = -dx
        scrollRemainder.x += dx
        scrollRemainder.y += dy
        let sx = Int16(clamping: Int(scrollRemainder.x.rounded(.towardZero)))
        let sy = Int16(clamping: Int(scrollRemainder.y.rounded(.towardZero)))
        guard sx != 0 || sy != 0 else { return }
        scrollRemainder.x -= CGFloat(sx)
        scrollRemainder.y -= CGFloat(sy)
        var d = Data()
        d.appendBE(sx)
        d.appendBE(sy)
        connection?.send(.mouseWheel, d)
    }

    // MARK: Keyboard

    private func sendKey(_ vk: UInt16, down: Bool) {
        var d = Data()
        d.appendBE(vk)
        d.append(down ? 1 : 0)
        connection?.send(.key, d)
    }

    /// Keyboard events come from an app-level event monitor (see main.swift), because
    /// AppKit swallows keyUp for keys pressed while Cmd is held and routes Cmd combos
    /// to menus. Returns true if the event was consumed.
    func handleKeyEvent(_ event: NSEvent) -> Bool {
        switch event.type {
        case .keyDown:
            guard let vk = KeyMap.windowsVK(for: event.keyCode) else { return true }
            heldKeys.insert(vk)
            sendKey(vk, down: true)  // repeats are sent too; Windows expects them
        case .keyUp:
            guard let vk = KeyMap.windowsVK(for: event.keyCode) else { return true }
            heldKeys.remove(vk)
            sendKey(vk, down: false)
        case .flagsChanged:
            modifierChanged(event.keyCode)
        default:
            return false
        }
        return true
    }

    private var heldKeys = Set<UInt16>()

    private func modifierChanged(_ code: UInt16) {
        guard let vk = KeyMap.windowsVK(for: code) else { return }
        if code == KeyMap.capsLockKeyCode {
            sendKey(vk, down: true)  // macOS only reports the toggle
            sendKey(vk, down: false)
            return
        }
        // Each flagsChanged for a key code means that key went down or up.
        if heldModifiers.contains(code) {
            heldModifiers.remove(code)
            sendKey(vk, down: false)
            if code == 55 || code == 54 {
                // Safety net: release anything pressed during the Cmd combo.
                for k in heldKeys { sendKey(k, down: false) }
                heldKeys.removeAll()
            }
        } else {
            heldModifiers.insert(code)
            sendKey(vk, down: true)
        }
    }

    /// Called when the window loses focus, so nothing stays held on the PC.
    func releaseAll() {
        heldModifiers.removeAll()
        heldKeys.removeAll()
        connection?.send(.releaseAll)
    }
}
