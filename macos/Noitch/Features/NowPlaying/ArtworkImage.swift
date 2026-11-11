import AppKit
import ImageIO

/// Artwork decoding and colour, shared by Home and the closed island.
enum ArtworkImage {
    /// Decodes straight to display size, so a large cover never exists in memory at full size.
    static func decode(_ data: Data, maxPixelSize: CGFloat) -> CGImage? {
        guard let source = CGImageSourceCreateWithData(data as CFData, nil) else { return nil }
        let options: [CFString: Any] = [
            kCGImageSourceCreateThumbnailFromImageAlways: true,
            kCGImageSourceCreateThumbnailWithTransform: true,
            kCGImageSourceThumbnailMaxPixelSize: maxPixelSize,
        ]
        return CGImageSourceCreateThumbnailAtIndex(source, 0, options as CFDictionary)
    }

    /// The stage's background: the cover averaged down to a few pixels, then smoothed up a little so
    /// the stretch across the page shows soft blends instead of straight creases. A few kilobytes.
    static func ambient(of image: CGImage, columns: Int = 6, rows: Int = 3, smooth: Int = 8) -> CGImage? {
        func draw(_ image: CGImage, width: Int, height: Int, quality: CGInterpolationQuality) -> CGImage? {
            guard let context = CGContext(
                data: nil, width: width, height: height, bitsPerComponent: 8, bytesPerRow: 0,
                space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGImageAlphaInfo.noneSkipLast.rawValue
            ) else { return nil }
            context.interpolationQuality = quality
            context.draw(image, in: CGRect(x: 0, y: 0, width: width, height: height))
            return context.makeImage()
        }
        guard let tiny = draw(image, width: columns, height: rows, quality: .medium) else { return nil }
        return draw(tiny, width: columns * smooth, height: rows * smooth, quality: .high)
    }

    /// Scaled to one pixel, then lifted so a dark cover still gives a visible bar.
    static func averageColor(of image: CGImage) -> NSColor? {
        var pixel = [UInt8](repeating: 0, count: 4)
        let drawn = pixel.withUnsafeMutableBytes { bytes -> Bool in
            guard let context = CGContext(
                data: bytes.baseAddress, width: 1, height: 1, bitsPerComponent: 8, bytesPerRow: 4,
                space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
            ) else { return false }
            context.interpolationQuality = .medium
            context.draw(image, in: CGRect(x: 0, y: 0, width: 1, height: 1))
            return true
        }
        guard drawn else { return nil }
        let color = NSColor(red: CGFloat(pixel[0]) / 255, green: CGFloat(pixel[1]) / 255, blue: CGFloat(pixel[2]) / 255, alpha: 1)
        var (hue, saturation, brightness, alpha): (CGFloat, CGFloat, CGFloat, CGFloat) = (0, 0, 0, 0)
        color.getHue(&hue, saturation: &saturation, brightness: &brightness, alpha: &alpha)
        return NSColor(hue: hue, saturation: saturation, brightness: max(brightness, 0.75), alpha: 1)
    }
}
