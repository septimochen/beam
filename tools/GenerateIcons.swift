import AppKit
import Foundation

// One geometric source feeds the Apple layers, Linux artwork, Android vectors,
// and monochrome marks. Runtime builds consume the checked-in exports.
struct Segment {
  let command: String
  let values: [CGFloat]
}

let rays: [[Segment]] = [
  [
    Segment(command: "M", values: [247, 455]),
    Segment(command: "Q", values: [233, 470, 252, 482]),
    Segment(command: "L", values: [291, 507]),
    Segment(command: "Q", values: [309, 518, 325, 502]),
    Segment(command: "L", values: [542, 282]),
    Segment(command: "Q", values: [561, 262, 536, 270]),
    Segment(command: "Z", values: []),
  ],
  [
    Segment(command: "M", values: [290, 558]),
    Segment(command: "Q", values: [268, 575, 292, 590]),
    Segment(command: "L", values: [406, 662]),
    Segment(command: "Q", values: [426, 675, 442, 655]),
    Segment(command: "L", values: [765, 284]),
    Segment(command: "Q", values: [787, 258, 755, 274]),
    Segment(command: "Z", values: []),
  ],
  [
    Segment(command: "M", values: [466, 715]),
    Segment(command: "Q", values: [449, 734, 472, 747]),
    Segment(command: "L", values: [520, 775]),
    Segment(command: "Q", values: [541, 787, 554, 765]),
    Segment(command: "L", values: [758, 431]),
    Segment(command: "Q", values: [774, 404, 751, 425]),
    Segment(command: "Z", values: []),
  ],
]
let lightColors = ["558EF8", "3265E8", "6E68D8"]
let darkColors = ["83B1FF", "6193FF", "A49AFF"]
guard CommandLine.arguments.count == 2 else {
  FileHandle.standardError.write(Data("Usage: GenerateIcons.swift PROJECT_ROOT\n".utf8))
  exit(1)
}
let root = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
let manager = FileManager.default

func number(_ value: CGFloat) -> String { String(format: "%.3f", Double(value)) }

func pathData(_ ray: [Segment], scale: CGFloat = 1) -> String {
  ray.map { segment in
    segment.command + segment.values.map { number(($0 - 512) * scale + 512) }.joined(separator: " ")
  }.joined(separator: " ")
}

func path(_ ray: [Segment]) -> CGPath {
  let result = CGMutablePath()
  for segment in ray {
    let v = segment.values
    switch segment.command {
    case "M": result.move(to: CGPoint(x: v[0], y: v[1]))
    case "L": result.addLine(to: CGPoint(x: v[0], y: v[1]))
    case "Q":
      result.addQuadCurve(
        to: CGPoint(x: v[2], y: v[3]), control: CGPoint(x: v[0], y: v[1]))
    default: result.closeSubpath()
    }
  }
  return result
}

func color(_ hex: String) -> CGColor {
  let rgb = UInt32(hex, radix: 16)!
  return CGColor(
    colorSpace: CGColorSpace(name: CGColorSpace.sRGB)!,
    components: [
      CGFloat((rgb >> 16) & 255) / 255, CGFloat((rgb >> 8) & 255) / 255,
      CGFloat(rgb & 255) / 255, 1,
    ])!
}

func write(_ text: String, _ relative: String) throws {
  let url = root.appendingPathComponent(relative)
  try manager.createDirectory(
    at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
  try (text + "\n").write(to: url, atomically: true, encoding: .utf8)
}

func draw(_ context: CGContext, size: CGFloat, dark: Bool = false, template: Bool = false) {
  context.translateBy(x: 0, y: size)
  context.scaleBy(x: size / 1024, y: -size / 1024)
  if template {
    context.translateBy(x: 512, y: 512)
    context.scaleBy(x: 1.35, y: 1.35)
    context.translateBy(x: -512, y: -512)
  } else {
    context.saveGState()
    context.addPath(
      CGPath(
        roundedRect: CGRect(x: 64, y: 64, width: 896, height: 896),
        cornerWidth: 202, cornerHeight: 202, transform: nil))
    context.clip()
    let colors = (dark ? ["272D3C", "121722"] : ["FAFCFF", "E2E8F5"]).map(color)
    let gradient = CGGradient(
      colorsSpace: CGColorSpace(name: CGColorSpace.sRGB), colors: colors as CFArray,
      locations: [0, 1])!
    context.drawLinearGradient(
      gradient, start: CGPoint(x: 512, y: 64), end: CGPoint(x: 512, y: 960), options: [])
    context.restoreGState()
  }
  for (index, ray) in rays.enumerated() {
    context.addPath(path(ray))
    context.setFillColor(
      template ? color("000000") : color((dark ? darkColors : lightColors)[index]))
    context.fillPath()
  }
}

func png(_ relative: String, size: Int, dark: Bool = false) throws {
  let context = CGContext(
    data: nil, width: size, height: size, bitsPerComponent: 8, bytesPerRow: 0,
    space: CGColorSpace(name: CGColorSpace.sRGB)!,
    bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
  draw(context, size: CGFloat(size), dark: dark)
  let rep = NSBitmapImageRep(cgImage: context.makeImage()!)
  let url = root.appendingPathComponent(relative)
  try manager.createDirectory(
    at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
  try rep.representation(using: .png, properties: [:])!.write(to: url)
}

let header =
  "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1024\" height=\"1024\" viewBox=\"0 0 1024 1024\">"
let svgPaths = rays.enumerated().map { index, ray in
  "<path fill=\"#\(lightColors[index])\" d=\"\(pathData(ray))\"/>"
}.joined(separator: "\n")
try write(
  """
  \(header)
  <defs><linearGradient id="base" x2="0" y2="1"><stop stop-color="#FAFCFF"/><stop offset="1" stop-color="#E2E8F5"/></linearGradient></defs>
  <rect x="64" y="64" width="896" height="896" rx="202" fill="url(#base)"/>
  \(svgPaths)
  </svg>
  """, "assets/icons/beam.svg")
try write(
  header + "\n" + rays.map { "<path d=\"\(pathData($0))\"/>" }.joined(separator: "\n") + "\n</svg>",
  "assets/icons/beam-symbolic.svg")
for (index, ray) in rays.enumerated() {
  try write(
    "\(header)\n<path fill=\"#\(lightColors[index])\" d=\"\(pathData(ray))\"/>\n</svg>",
    "assets/icons/Beam.icon/Assets/ray-\(index + 1).svg")
}

func nativeColor(_ hex: String) -> [String: String] {
  let rgb = UInt32(hex, radix: 16)!
  return [
    "solid": "srgb:\(number(CGFloat((rgb >> 16) & 255) / 255)),"
      + "\(number(CGFloat((rgb >> 8) & 255) / 255)),"
      + "\(number(CGFloat(rgb & 255) / 255)),1.000"
  ]
}
let layers: [[String: Any]] = rays.indices.map { index in
  [
    "image-name": "ray-\(index + 1).svg", "name": "Ray \(index + 1)",
    "fill-specializations": [
      ["value": nativeColor(lightColors[index])],
      ["appearance": "dark", "value": nativeColor(darkColors[index])],
    ],
  ]
}
let document: [String: Any] = [
  "color-space-for-untagged-svg-colors": "srgb",
  "fill-specializations": [
    ["value": nativeColor("EFF3FC")],
    ["appearance": "dark", "value": nativeColor("1B2130")],
  ],
  "groups": [
    [
      "name": "Beam rays", "layers": layers, "specular": true,
      "shadow": ["kind": "neutral", "opacity": 0.25],
      "translucency": ["enabled": false, "value": 0.3],
    ]
  ],
  "supported-platforms": ["squares": "shared"],
]
let json = try JSONSerialization.data(
  withJSONObject: document, options: [.prettyPrinted, .sortedKeys])
try write(String(decoding: json, as: UTF8.self), "assets/icons/Beam.icon/icon.json")
try png("assets/icons/beam.png", size: 512)
try png("assets/icons/beam-dark.png", size: 512, dark: true)
let iconset = "build/icons/Beam.iconset"
for size in [16, 32, 128, 256, 512] {
  try png("\(iconset)/icon_\(size)x\(size).png", size: size)
  try png("\(iconset)/icon_\(size)x\(size)@2x.png", size: size * 2)
}
let iconutil = Process()
iconutil.executableURL = URL(fileURLWithPath: "/usr/bin/iconutil")
iconutil.arguments = [
  "-c", "icns", root.appendingPathComponent(iconset).path, "-o",
  root.appendingPathComponent("assets/icons/Beam.icns").path,
]
try iconutil.run()
iconutil.waitUntilExit()
guard iconutil.terminationStatus == 0 else {
  FileHandle.standardError.write(Data("iconutil failed to package Beam.icns\n".utf8))
  exit(iconutil.terminationStatus)
}
let pdfURL = root.appendingPathComponent("assets/icons/BeamTemplate.pdf")
let consumer = CGDataConsumer(url: pdfURL as CFURL)!
var box = CGRect(x: 0, y: 0, width: 18, height: 18)
let pdf = CGContext(consumer: consumer, mediaBox: &box, nil)!
pdf.beginPDFPage(nil)
draw(pdf, size: 18, template: true)
pdf.endPDFPage()
pdf.closePDF()

let vectorHeader = """
  <vector xmlns:android="http://schemas.android.com/apk/res/android"
      android:width="108dp" android:height="108dp"
      android:viewportWidth="1024" android:viewportHeight="1024">
  """
for monochrome in [false, true] {
  let paths = rays.enumerated().map { index, ray in
    let fill = monochrome ? "#FFFFFFFF" : "@color/beam_icon_ray_\(index + 1)"
    return
      "    <path android:fillColor=\"\(fill)\" android:pathData=\"\(pathData(ray, scale: 0.86))\"/>"
  }.joined(separator: "\n")
  try write(
    "\(vectorHeader)\n\(paths)\n</vector>",
    "android/app/src/main/res/drawable/ic_beam_\(monochrome ? "monochrome" : "foreground").xml")
}
for dark in [false, true] {
  let colors = dark ? darkColors : lightColors
  let entries = colors.enumerated().map { index, hex in
    "    <color name=\"beam_icon_ray_\(index + 1)\">#\(hex)</color>"
  }.joined(separator: "\n")
  try write(
    """
    <resources>
        <color name="beam_icon_background">#\(dark ? "1B2130" : "EFF3FC")</color>
    \(entries)
    </resources>
    """, "android/app/src/main/res/values\(dark ? "-night" : "")/icon_colors.xml")
}
print("Generated Beam vector layers, platform icons, and monochrome marks")
