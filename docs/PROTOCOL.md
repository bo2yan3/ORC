# ORC wire protocol — version 1

One TCP connection per session (default port **47800**). The host is the
server; the client connects. All integers are **big-endian**. `TCP_NODELAY`
is set on both ends.

## Framing

Every message is an 8-byte header followed by `length` payload bytes.

| Offset | Size | Field |
|---|---|---|
| 0 | u8 | `type` |
| 1 | u8 | `flags` |
| 2 | u16 | reserved (0) |
| 4 | u32 | `length` of payload (max 16 MiB) |

## Message types

| Type | Name | Direction | Payload |
|---|---|---|---|
| 1 | `HELLO` | C→H | `"ORC1"` (4 bytes), u16 `version`=1, u16 `token_len`, `token` bytes (UTF-8) |
| 2 | `HELLO_ACK` | H→C | u8 `status` (0 ok, 1 bad token, 2 bad version), u16 `width`, u16 `height`, u8 `codec` (1 = HEVC) |
| 3 | `VIDEO` | H→C | u64 `pts_us`, then one HEVC access unit in Annex-B format. `flags` bit 0 = keyframe |
| 4 | `KEYFRAME_REQ` | C→H | empty |
| 5 | `PING` | either | u64 `t_us` (sender's clock) |
| 6 | `PONG` | either | the PING payload echoed back |
| 16 | `MOUSE_MOVE` | C→H | u16 `x`, u16 `y` — absolute, normalized 0–65535 across the streamed display |
| 17 | `MOUSE_BUTTON` | C→H | u8 `button` (0 left, 1 right, 2 middle, 3 back, 4 forward), u8 `down` |
| 18 | `MOUSE_WHEEL` | C→H | i16 `dx`, i16 `dy` in Windows wheel units (120 = one notch; smaller values are smooth scroll) |
| 19 | `KEY` | C→H | u16 Windows virtual-key code, u8 `down` |
| 20 | `RELEASE_ALL` | C→H | empty — host releases every key and button it is holding |

## Session

1. Client sends `HELLO`. Host replies `HELLO_ACK`. On a non-zero status the
   host closes the connection.
2. Host streams `VIDEO`. The first frame after `HELLO_ACK` is always a keyframe
   and carries VPS/SPS/PPS inline.
3. Client sends input whenever it has it, and `KEYFRAME_REQ` if decoding fails.
4. On disconnect the host releases all held input (no stuck Ctrl keys).

## Notes

- Unknown message types must be ignored (skip `length` bytes), so new types
  can be added without breaking old peers.
- The host does not send a frame when the screen hasn't changed; the client
  keeps showing the last one.
