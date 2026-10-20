import Foundation
import Network

/// TCP connection to an orc-host. Callbacks arrive on the main queue.
final class Connection {
    var onMessage: ((Message) -> Void)?
    var onClosed: ((String) -> Void)?

    private let conn: NWConnection
    private var buffer = Data()
    private var closed = false

    init(host: String, port: UInt16) {
        let tcp = NWProtocolTCP.Options()
        tcp.noDelay = true
        let params = NWParameters(tls: nil, tcp: tcp)
        params.serviceClass = .interactiveVideo
        conn = NWConnection(host: NWEndpoint.Host(host),
                            port: NWEndpoint.Port(rawValue: port)!,
                            using: params)
    }

    func start(token: String) {
        conn.stateUpdateHandler = { [weak self] state in
            guard let self else { return }
            switch state {
            case .ready:
                self.sendHello(token: token)
                self.receiveLoop()
            case .failed(let err):
                self.close(reason: "connection failed: \(err)")
            case .waiting(let err):
                // e.g. host not running yet, or macOS local-network permission pending
                print("waiting: \(err)")
            default:
                break
            }
        }
        conn.start(queue: .main)
    }

    func send(_ type: MsgType, flags: UInt8 = 0, _ payload: Data = Data()) {
        guard !closed else { return }
        conn.send(content: frame(type, flags: flags, payload), completion: .contentProcessed { [weak self] err in
            if let err { self?.close(reason: "send failed: \(err)") }
        })
    }

    func close(reason: String) {
        guard !closed else { return }
        closed = true
        conn.cancel()
        onClosed?(reason)
    }

    private func sendHello(token: String) {
        var p = Data("ORC1".utf8)
        p.appendBE(Proto.version)
        let t = Data(token.utf8)
        p.appendBE(UInt16(t.count))
        p.append(t)
        send(.hello, p)
    }

    private func receiveLoop() {
        conn.receive(minimumIncompleteLength: 1, maximumLength: 1 << 20) { [weak self] data, _, isComplete, error in
            guard let self, !self.closed else { return }
            if let data, !data.isEmpty {
                self.buffer.append(data)
                self.drain()
            }
            if let error { self.close(reason: "receive error: \(error)"); return }
            if isComplete { self.close(reason: "host closed the connection"); return }
            self.receiveLoop()
        }
    }

    private func drain() {
        while buffer.count >= Proto.headerSize {
            let len = Int(buffer.readU32(4))
            guard len <= Proto.maxPayload else { close(reason: "oversized message"); return }
            guard buffer.count >= Proto.headerSize + len else { return }
            let s = buffer.startIndex
            let msg = Message(type: buffer[s], flags: buffer[s + 1],
                              payload: Data(buffer[(s + Proto.headerSize)..<(s + Proto.headerSize + len)]))
            buffer.removeFirst(Proto.headerSize + len)
            onMessage?(msg)
            if closed { return }
        }
    }
}
