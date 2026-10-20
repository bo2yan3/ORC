import CoreMedia
import Foundation

/// Turns Annex-B HEVC access units into CMSampleBuffers that
/// AVSampleBufferDisplayLayer can decode (with VideoToolbox, in hardware).
final class HEVCDepacketizer {
    private var vps: Data?
    private var sps: Data?
    private var pps: Data?
    private(set) var format: CMVideoFormatDescription?

    enum NALType {
        static let vps: UInt8 = 32
        static let sps: UInt8 = 33
        static let pps: UInt8 = 34
        static let aud: UInt8 = 35
    }

    /// Splits an Annex-B byte stream (00 00 01 / 00 00 00 01 start codes) into NAL units.
    static func splitNALs(_ data: Data) -> [Data] {
        var nals: [Data] = []
        let bytes = [UInt8](data)
        let n = bytes.count
        var i = 0
        var start = -1
        while i + 2 < n {
            if bytes[i] == 0, bytes[i + 1] == 0, bytes[i + 2] == 1 {
                if start >= 0 {
                    var end = i
                    if end > start, bytes[end - 1] == 0 { end -= 1 }  // 4-byte start code
                    if end > start { nals.append(Data(bytes[start..<end])) }
                }
                i += 3
                start = i
            } else {
                i += 1
            }
        }
        if start >= 0, start < n { nals.append(Data(bytes[start..<n])) }
        return nals
    }

    static func nalType(_ nal: Data) -> UInt8 { (nal[nal.startIndex] >> 1) & 0x3F }

    /// Returns a sample ready to enqueue, or nil if there is nothing to show yet
    /// (e.g. we haven't seen parameter sets).
    func sampleBuffer(fromAccessUnit au: Data) -> CMSampleBuffer? {
        var slices: [Data] = []
        var paramsChanged = false

        for nal in Self.splitNALs(au) where !nal.isEmpty {
            switch Self.nalType(nal) {
            case NALType.vps: if nal != vps { vps = nal; paramsChanged = true }
            case NALType.sps: if nal != sps { sps = nal; paramsChanged = true }
            case NALType.pps: if nal != pps { pps = nal; paramsChanged = true }
            case NALType.aud: continue
            default: slices.append(nal)
            }
        }
        if paramsChanged || format == nil { rebuildFormat() }
        guard let format, !slices.isEmpty else { return nil }

        // Length-prefixed ("hvcC" style) sample: 4-byte big-endian size before each NAL.
        var sample = Data()
        for nal in slices {
            sample.appendBE(UInt32(nal.count))
            sample.append(nal)
        }
        return makeSampleBuffer(sample, format: format)
    }

    private func rebuildFormat() {
        guard let vps, let sps, let pps else { return }
        let sets = [vps, sps, pps].map { [UInt8]($0) }
        var desc: CMVideoFormatDescription?
        let status = sets[0].withUnsafeBufferPointer { v in
            sets[1].withUnsafeBufferPointer { s in
                sets[2].withUnsafeBufferPointer { p in
                    let ptrs = [v.baseAddress!, s.baseAddress!, p.baseAddress!]
                    let sizes = [v.count, s.count, p.count]
                    return CMVideoFormatDescriptionCreateFromHEVCParameterSets(
                        allocator: kCFAllocatorDefault,
                        parameterSetCount: 3,
                        parameterSetPointers: ptrs,
                        parameterSetSizes: sizes,
                        nalUnitHeaderLength: 4,
                        extensions: nil,
                        formatDescriptionOut: &desc)
                }
            }
        }
        if status == noErr, let desc {
            format = desc
            let dims = CMVideoFormatDescriptionGetDimensions(desc)
            print("video format: HEVC \(dims.width)x\(dims.height)")
        } else {
            print("could not build HEVC format description (status \(status))")
        }
    }

    private func makeSampleBuffer(_ sample: Data, format: CMVideoFormatDescription) -> CMSampleBuffer? {
        var block: CMBlockBuffer?
        var status = CMBlockBufferCreateWithMemoryBlock(
            allocator: kCFAllocatorDefault, memoryBlock: nil, blockLength: sample.count,
            blockAllocator: kCFAllocatorDefault, customBlockSource: nil, offsetToData: 0,
            dataLength: sample.count, flags: kCMBlockBufferAssureMemoryNowFlag, blockBufferOut: &block)
        guard status == kCMBlockBufferNoErr, let block else { return nil }

        status = sample.withUnsafeBytes { raw in
            CMBlockBufferReplaceDataBytes(with: raw.baseAddress!, blockBuffer: block,
                                          offsetIntoDestination: 0, dataLength: sample.count)
        }
        guard status == kCMBlockBufferNoErr else { return nil }

        var sb: CMSampleBuffer?
        var size = sample.count
        status = CMSampleBufferCreateReady(
            allocator: kCFAllocatorDefault, dataBuffer: block, formatDescription: format,
            sampleCount: 1, sampleTimingEntryCount: 0, sampleTimingArray: nil,
            sampleSizeEntryCount: 1, sampleSizeArray: &size, sampleBufferOut: &sb)
        guard status == noErr, let sb else { return nil }

        // Show as soon as it's decoded; never buffer for smooth playback.
        if let attachments = CMSampleBufferGetSampleAttachmentsArray(sb, createIfNecessary: true),
           CFArrayGetCount(attachments) > 0 {
            let dict = unsafeBitCast(CFArrayGetValueAtIndex(attachments, 0), to: CFMutableDictionary.self)
            CFDictionarySetValue(dict,
                                 Unmanaged.passUnretained(kCMSampleAttachmentKey_DisplayImmediately).toOpaque(),
                                 Unmanaged.passUnretained(kCFBooleanTrue).toOpaque())
        }
        return sb
    }
}
