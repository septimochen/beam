import Foundation

struct LaunchOptions {
  var stateDirectory: String?
  var output = ""
  var port = 4269
  var advertise = true
  var help = false
  var version = false
  var timeout = 30000

  init(arguments: [String], configuration: [String: Any] = [:]) throws {
    output = configuration["output"] as? String ?? ""
    port = configuration["port"] as? Int ?? 4269
    advertise = configuration["advertise"] as? Bool ?? true
    timeout = configuration["timeout"] as? Int ?? 30000
    var index = 0
    while index < arguments.count {
      let argument = arguments[index]
      switch argument {
      case "--help", "-h": help = true
      case "--version", "-v": version = true
      case "--no-discovery": advertise = false
      case "--state-dir", "--output", "--listen":
        index += 1
        guard index < arguments.count else { throw invalid("Missing value for \(argument)") }
        let value = arguments[index]
        guard !value.isEmpty, !value.contains("\0") else {
          throw invalid("Invalid value for \(argument)")
        }
        if argument == "--state-dir" { stateDirectory = value }
        if argument == "--output" { output = value }
        if argument == "--listen" {
          guard value.allSatisfy({ $0.isASCII && $0.isNumber }), let number = Int(value),
            (1...65535).contains(number)
          else {
            throw invalid("Listen port must be between 1 and 65535")
          }
          port = number
        }
      default:
        throw invalid("Unknown argument: \(argument)")
      }
      index += 1
    }
  }

  private func invalid(_ message: String) -> NSError {
    NSError(domain: "org.beam.desktop", code: -1, userInfo: [NSLocalizedDescriptionKey: message])
  }

  static let usage = """
    Beam native macOS app for explicitly paired devices.
    Usage: beam-desktop [--state-dir DIR] [--output DIR] [--listen PORT] [--no-discovery]
    --help, -h       Show this help
    --version, -v    Show version
    Receives on launch using --output or the remembered receive folder after pairing.
    Closing hides the window. Use Quit Beam to cancel and join workers.
    """
}
