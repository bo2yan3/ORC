// ORC wire protocol v1 — see docs/PROTOCOL.md
import Foundation

enum MsgType: UInt8 {
    case hello = 1
    case helloAck = 2
    case video = 3
    case keyframeReq = 4
    case ping = 5
    case pong = 6
    case mouseMove = 16
    case mouseButton = 17
    case mouseWheel = 18
    case key = 19
    case releaseAll = 20
}

enum Proto {
    static let defaultPort: UInt16 = 47800
    static let version: UInt16 = 1
    static let headerSize = 8
    static let maxPayload = 16 * 1024 * 1024
    static let flagKeyframe: UInt8 = 0x01
}

struct Message {
    let type: UInt8  // raw, so unknown types can be skipped
    let flags: UInt8
    let payload: Data
}

extension Data {
    mutating func appendBE(_ v: UInt16) { append(UInt8(v >> 8)); append(UInt8(v & 0xFF)) }
    mutating func appendBE(_ v: Int16) { appendBE(UInt16(bitPattern: v)) }
    mutating func appendBE(_ v: UInt32) {
        for s in stride(from: 24, through: 0, by: -8) { append(UInt8((v >> UInt32(s)) & 0xFF)) }
    }
    mutating func appendBE(_ v: UInt64) {
        for s in stride(from: 56, through: 0, by: -8) { append(UInt8((v >> UInt64(s)) & 0xFF)) }
    }

    func readU16(_ at: Int) -> UInt16 {
        let i = startIndex + at
        return UInt16(self[i]) << 8 | UInt16(self[i + 1])
    }
    func readU32(_ at: Int) -> UInt32 {
        let i = startIndex + at
        return UInt32(self[i]) << 24 | UInt32(self[i + 1]) << 16 | UInt32(self[i + 2]) << 8 | UInt32(self[i + 3])
    }
    func readU64(_ at: Int) -> UInt64 {
        var v: UInt64 = 0
        for k in 0..<8 { v = v << 8 | UInt64(self[startIndex + at + k]) }
        return v
    }
}

func frame(_ type: MsgType, flags: UInt8 = 0, _ payload: Data = Data()) -> Data {
    var d = Data(capacity: Proto.headerSize + payload.count)
    d.append(type.rawValue)
    d.append(flags)
    d.appendBE(UInt16(0))
    d.appendBE(UInt32(payload.count))
    d.append(payload)
    return d
}
