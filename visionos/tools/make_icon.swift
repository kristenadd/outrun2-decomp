// visionOS app icon (visionos/Assets.xcassets/AppIcon.solidimagestack) from
// the repository's icon.png, in three 1024x1024 layers for the parallax of
// visionOS icons. Front: the car (the image's main subject, Vision's
// foreground mask). Middle: the title (its blue / chrome lettering over the
// Back: the scene, opaque, the car softened
// and the title softened away so their edges don't show twice when the layers move.
// Run on a Mac (macOS 14+): swift visionos/tools/make_icon.swift [icon.png] [out dir]
import AppKit
import CoreImage
import CoreImage.CIFilterBuiltins
import Vision

let args = CommandLine.arguments
let source = URL(fileURLWithPath: args.count > 1 ? args[1] : "icon.png")
let out = URL(fileURLWithPath: args.count > 2 ? args[2] : "visionos/Assets.xcassets/AppIcon.solidimagestack")
let size: CGFloat = 1024
let context = CIContext()

guard let input = CIImage(contentsOf: source) else { fatalError("cannot read \(source.path)") }
let image = input.transformed(by: CGAffineTransform(scaleX: size / input.extent.width, y: size / input.extent.height))
let bounds = CGRect(x: 0, y: 0, width: size, height: size)

// The subject mask (the car, with its passengers).
func subjectMask(_ image: CIImage) throws -> CIImage {
    let request = VNGenerateForegroundInstanceMaskRequest()
    let handler = VNImageRequestHandler(ciImage: image)
    try handler.perform([request])
    guard let result = request.results?.first else { fatalError("no subject found") }
    // The largest instance: the car (the sign and the logo can be found too).
    var best = result.allInstances.first!, bestArea = 0
    for instance in result.allInstances {
        let m = try result.generateScaledMaskForImage(forInstances: [instance], from: handler)
        let area = CVPixelBufferGetWidth(m) * CVPixelBufferGetHeight(m)
        let ci = CIImage(cvPixelBuffer: m)
        let avg = CIFilter.areaAverage(); avg.inputImage = ci; avg.extent = ci.extent
        var px = [Float](repeating: 0, count: 4)
        context.render(avg.outputImage!, toBitmap: &px, rowBytes: 16, bounds: CGRect(x: 0, y: 0, width: 1, height: 1), format: .RGBAf, colorSpace: nil)
        let covered = Int(Float(area) * px[0])
        if covered > bestArea { bestArea = covered; best = instance }
    }
    let mask = try result.generateScaledMaskForImage(forInstances: [best], from: handler)
    return CIImage(cvPixelBuffer: mask)
}

// The title: the subject of the picture's top band (the lettering and its
// palm trees over the sky), its mask placed back over the whole picture.
func titleMask(_ image: CIImage) throws -> CIImage {
    let band = CGRect(x: 0, y: size * 0.58, width: size, height: size * 0.42)   // CI: y up, the top of the picture
    let crop = image.cropped(to: band).transformed(by: CGAffineTransform(translationX: 0, y: -band.minY))
    let request = VNGenerateForegroundInstanceMaskRequest()
    let handler = VNImageRequestHandler(ciImage: crop)
    try handler.perform([request])
    guard let result = request.results?.first else { fatalError("no title found") }
    let mask = CIImage(cvPixelBuffer: try result.generateScaledMaskForImage(forInstances: result.allInstances, from: handler))
    let placed = mask.transformed(by: CGAffineTransform(translationX: 0, y: band.minY))
        .composited(over: CIImage(color: .black).cropped(to: bounds)).cropped(to: bounds)
    // Closed: the glossy letter fills look like sky to the mask (holes inside the letters).
    let grow = CIFilter.morphologyMaximum(); grow.inputImage = placed; grow.radius = 14
    let shrink = CIFilter.morphologyMinimum(); shrink.inputImage = grow.outputImage!; shrink.radius = 14
    return shrink.outputImage!.cropped(to: bounds)
}

func masked(_ image: CIImage, _ mask: CIImage) -> CIImage {
    let blend = CIFilter.blendWithMask()
    blend.inputImage = image; blend.backgroundImage = CIImage(color: .clear).cropped(to: bounds); blend.maskImage = mask
    return blend.outputImage!.cropped(to: bounds)
}

func write(_ image: CIImage, _ layer: String, opaque: Bool) throws {
    let dir = out.appendingPathComponent("\(layer).solidimagestacklayer/Content.imageset")
    try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
    let file = dir.appendingPathComponent("\(layer.lowercased()).png")
    let space = CGColorSpace(name: CGColorSpace.sRGB)!
    try context.writePNGRepresentation(of: image.cropped(to: bounds), to: file, format: opaque ? .RGBA8 : .RGBA8, colorSpace: space)
    try """
    {
      "images" : [ { "filename" : "\(layer.lowercased()).png", "idiom" : "vision", "scale" : "2x" } ],
      "info" : { "author" : "xcode", "version" : 1 }
    }
    """.write(to: dir.appendingPathComponent("Contents.json"), atomically: true, encoding: .utf8)
    try """
    { "info" : { "author" : "xcode", "version" : 1 } }
    """.write(to: dir.deletingLastPathComponent().appendingPathComponent("Contents.json"), atomically: true, encoding: .utf8)
}

let car = try subjectMask(image)
let title = try titleMask(image)
// The car's and the title's areas in the scene: filled from a heavy blur of their surroundings.
let both = CIFilter.maximumCompositing(); both.inputImage = car; both.backgroundImage = title
let grown = CIFilter.morphologyMaximum(); grown.inputImage = both.outputImage!.cropped(to: bounds); grown.radius = 6
let hole = CIFilter.gaussianBlur(); hole.inputImage = grown.outputImage!.clampedToExtent(); hole.radius = 5
let fill = CIFilter.gaussianBlur(); fill.inputImage = image.clampedToExtent(); fill.radius = 40
let back = CIFilter.blendWithMask(); back.inputImage = fill.outputImage!.cropped(to: bounds); back.backgroundImage = image; back.maskImage = hole.outputImage!.cropped(to: bounds)

try write(masked(image, car), "Front", opaque: false)
try write(masked(image, title), "Middle", opaque: false)
try write(back.outputImage!, "Back", opaque: true)
try """
{
  "info" : { "author" : "xcode", "version" : 1 },
  "layers" : [ { "filename" : "Front.solidimagestacklayer" }, { "filename" : "Middle.solidimagestacklayer" }, { "filename" : "Back.solidimagestacklayer" } ]
}
""".write(to: out.appendingPathComponent("Contents.json"), atomically: true, encoding: .utf8)
try """
{ "info" : { "author" : "xcode", "version" : 1 } }
""".write(to: out.deletingLastPathComponent().appendingPathComponent("Contents.json"), atomically: true, encoding: .utf8)
print("visionOS icon layers in \(out.path)")
