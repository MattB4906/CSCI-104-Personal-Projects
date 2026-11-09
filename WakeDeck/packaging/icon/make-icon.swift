// Draws WakeDeck's icon as a 1024 x 1024 PNG with CoreGraphics only.
// Usage: swift make-icon.swift <output.png>
// make-icns.sh turns the PNG into WakeDeck.icns.
import CoreGraphics
import Foundation
import ImageIO
import UniformTypeIdentifiers

let size = 1024
let outputPath = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "WakeDeck-1024.png"

let colorSpace = CGColorSpace(name: CGColorSpace.sRGB)!
guard let ctx = CGContext(data: nil, width: size, height: size, bitsPerComponent: 8, bytesPerRow: 0,
                          space: colorSpace, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else {
    fatalError("cannot create the drawing context")
}

func rgb(_ r: CGFloat, _ g: CGFloat, _ b: CGFloat, _ a: CGFloat = 1) -> CGColor {
    CGColor(colorSpace: colorSpace, components: [r / 255, g / 255, b / 255, a])!
}

// macOS icon grid: the shape sits inside a 1024 canvas with about 100 px of margin.
let inset: CGFloat = 100
let tile = CGRect(x: inset, y: inset, width: CGFloat(size) - 2 * inset, height: CGFloat(size) - 2 * inset)
let radius: CGFloat = 185
let tilePath = CGPath(roundedRect: tile, cornerWidth: radius, cornerHeight: radius, transform: nil)

// Soft shadow under the tile.
ctx.saveGState()
ctx.setShadow(offset: CGSize(width: 0, height: -12), blur: 28, color: rgb(0, 0, 0, 0.35))
ctx.addPath(tilePath)
ctx.setFillColor(rgb(15, 23, 42))
ctx.fillPath()
ctx.restoreGState()

// Night-to-dawn gradient: deep navy at the bottom rising to teal at the top.
ctx.saveGState()
ctx.addPath(tilePath)
ctx.clip()
let gradient = CGGradient(colorsSpace: colorSpace,
                          colors: [rgb(17, 24, 64), rgb(30, 64, 140), rgb(13, 148, 168)] as CFArray,
                          locations: [0, 0.55, 1])!
// CoreGraphics' origin is bottom-left: the start point is the bottom of the tile.
ctx.drawLinearGradient(gradient, start: CGPoint(x: 512, y: tile.minY), end: CGPoint(x: 512, y: tile.maxY), options: [])
ctx.restoreGState()

let white = rgb(255, 255, 255)
ctx.setStrokeColor(white)
ctx.setLineCap(.round)

// Power symbol: an open ring with a vertical bar, slightly below centre.
let center = CGPoint(x: 512, y: 440)
let ringRadius: CGFloat = 190
let gap: CGFloat = .pi / 3.6  // opening at the top, on each side of the bar
ctx.setLineWidth(58)
ctx.addArc(center: center, radius: ringRadius, startAngle: .pi / 2 + gap, endAngle: .pi / 2 - gap, clockwise: false)
ctx.strokePath()
ctx.move(to: CGPoint(x: center.x, y: center.y + 40))
ctx.addLine(to: CGPoint(x: center.x, y: center.y + ringRadius + 40))
ctx.strokePath()

// The wake signal: three arcs radiating upwards above the bar, fading out.
let signalCenter = CGPoint(x: 512, y: center.y + ringRadius + 40)
let arcs: [(radius: CGFloat, alpha: CGFloat)] = [(95, 0.95), (165, 0.7), (235, 0.45)]
ctx.setLineWidth(34)
for arc in arcs {
    ctx.setStrokeColor(rgb(255, 255, 255, arc.alpha))
    ctx.addArc(center: signalCenter, radius: arc.radius, startAngle: .pi / 2 - 0.62, endAngle: .pi / 2 + 0.62, clockwise: false)
    ctx.strokePath()
}

guard let image = ctx.makeImage(),
      let destination = CGImageDestinationCreateWithURL(URL(fileURLWithPath: outputPath) as CFURL,
                                                        UTType.png.identifier as CFString, 1, nil) else {
    fatalError("cannot create \(outputPath)")
}
CGImageDestinationAddImage(destination, image, nil)
guard CGImageDestinationFinalize(destination) else { fatalError("cannot write \(outputPath)") }
print("wrote \(outputPath)")
