import AppKit

/// macOS virtual key codes (ANSI layout) -> Windows virtual-key codes.
/// Cmd is sent as Ctrl so Cmd+C / Cmd+S / Cmd+Z behave like on a Mac.
enum KeyMap {
    static let table: [UInt16: UInt16] = {
        var m: [UInt16: UInt16] = [:]
        // Letters
        let letters: [(UInt16, Character)] = [
            (0, "A"), (11, "B"), (8, "C"), (2, "D"), (14, "E"), (3, "F"), (5, "G"), (4, "H"),
            (34, "I"), (38, "J"), (40, "K"), (37, "L"), (46, "M"), (45, "N"), (31, "O"), (35, "P"),
            (12, "Q"), (15, "R"), (1, "S"), (17, "T"), (32, "U"), (9, "V"), (13, "W"), (7, "X"),
            (16, "Y"), (6, "Z"),
        ]
        for (code, ch) in letters { m[code] = UInt16(ch.asciiValue!) }
        // Number row
        let digits: [(UInt16, UInt16)] = [
            (29, 0x30), (18, 0x31), (19, 0x32), (20, 0x33), (21, 0x34),
            (23, 0x35), (22, 0x36), (26, 0x37), (28, 0x38), (25, 0x39),
        ]
        for (code, vk) in digits { m[code] = vk }
        // Punctuation (VK_OEM_*)
        m[41] = 0xBA  // ;
        m[24] = 0xBB  // =
        m[43] = 0xBC  // ,
        m[27] = 0xBD  // -
        m[47] = 0xBE  // .
        m[44] = 0xBF  // /
        m[50] = 0xC0  // `
        m[33] = 0xDB  // [
        m[42] = 0xDC  // \
        m[30] = 0xDD  // ]
        m[39] = 0xDE  // '
        // Editing / navigation
        m[36] = 0x0D  // Return
        m[76] = 0x0D  // keypad Enter
        m[48] = 0x09  // Tab
        m[49] = 0x20  // Space
        m[51] = 0x08  // Delete (backspace)
        m[117] = 0x2E // Forward delete
        m[53] = 0x1B  // Escape
        m[115] = 0x24 // Home
        m[119] = 0x23 // End
        m[116] = 0x21 // Page Up
        m[121] = 0x22 // Page Down
        m[123] = 0x25 // Left
        m[126] = 0x26 // Up
        m[124] = 0x27 // Right
        m[125] = 0x28 // Down
        // Function keys
        let fkeys: [UInt16] = [122, 120, 99, 118, 96, 97, 98, 100, 101, 109, 103, 111]
        for (i, code) in fkeys.enumerated() { m[code] = 0x70 + UInt16(i) }
        // Modifiers
        m[56] = 0xA0  // Left Shift
        m[60] = 0xA1  // Right Shift
        m[59] = 0xA2  // Left Control  -> Left Ctrl
        m[62] = 0xA3  // Right Control -> Right Ctrl
        m[55] = 0xA2  // Left Command  -> Left Ctrl
        m[54] = 0xA3  // Right Command -> Right Ctrl
        m[58] = 0xA4  // Left Option   -> Left Alt
        m[61] = 0xA5  // Right Option  -> Right Alt
        m[57] = 0x14  // Caps Lock
        return m
    }()

    static func windowsVK(for keyCode: UInt16) -> UInt16? { table[keyCode] }

    static let capsLockKeyCode: UInt16 = 57
}
