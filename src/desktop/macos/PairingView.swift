import SwiftUI

struct PairingView: View {
  @ObservedObject var model: DesktopModel
  @State private var selected = ""
  @State private var pin = ""
  @State private var showQR = false

  private func value(_ index: Int) -> String {
    model.pairingValues.indices.contains(index) ? model.pairingValues[index] : ""
  }

  var body: some View {
    ScrollView {
      VStack(alignment: .leading, spacing: 14) {
        Text("Add device").font(.title.bold())
        Text("Keep both devices on the same local network and open Add device on each.")
        Text("\(value(0)): \(value(1))").textSelection(.enabled)
        if !model.pairingError.isEmpty { Text(model.pairingError).foregroundStyle(.red) }
        if value(0) == "hosting" {
          Text("PIN: \(value(3))").font(.title.monospaced())
          HStack {
            Button("Show QR code") { showQR.toggle() }
            Button("Copy invitation") { model.copyText(value(2)) }
          }
          if showQR {
            let rows = value(7).split(separator: "\n").map { Array($0) }
            Canvas { context, size in
              context.fill(Path(CGRect(origin: .zero, size: size)), with: .color(.white))
              let scale = min(size.width, size.height) / CGFloat(rows.count + 8)
              for y in rows.indices {
                for x in rows[y].indices where rows[y][x] == "1" {
                  context.fill(
                    Path(
                      CGRect(
                        x: CGFloat(x + 4) * scale, y: CGFloat(y + 4) * scale, width: scale,
                        height: scale)),
                    with: .color(.black))
                }
              }
            }.frame(width: 320, height: 320).accessibilityLabel(
              "Scan this pairing QR code with Beam on Android")
          }
        }
        if value(0) == "awaiting approval" {
          Text("Pair with \(value(4))?").font(.headline)
          TextField("Local device alias", text: $model.pairingAlias)
          Button("Approve device") { model.pairingAction("approve", [model.pairingAlias]) }
            .buttonStyle(.borderedProminent)
          Button("Decline") { model.pairingAction("reject") }
        } else {
          HStack {
            Button("Find nearby devices") { model.pairingAction("browse") }
            Button("New invitation") {
              model.pairingAction("host", [Host.current().localizedName ?? "Beam Mac"])
            }
          }
          let indexes = Array(stride(from: 8, to: max(8, model.pairingValues.count - 1), by: 2))
          Picker("Nearby device", selection: $selected) {
            Text("Choose a device").tag("")
            ForEach(indexes, id: \.self) { index in Text(value(index)).tag(value(index + 1)) }
          }
          HStack {
            TextField("PIN shown on the other device", text: $pin)
            Button("Pair nearby") { model.pairingAction("pin", [selected, pin]) }
              .disabled(selected.isEmpty || pin.count != 6)
          }
          TextField("Paste a Beam invitation", text: $model.pairingInvitation)
            .onChange(of: model.pairingInvitation) { _, value in
              if value.count > 2048 { model.pairingInvitation = String(value.prefix(2048)) }
            }
          Button("Use invitation") {
            model.pairingAction(
              "join", [model.pairingInvitation.trimmingCharacters(in: .whitespacesAndNewlines)])
          }
          .disabled(model.pairingInvitation.isEmpty)
        }
        if value(0) == "incomplete" {
          Button("Forget pending approval") { model.pairingAction("forget") }
        }
        Button("Done", action: model.closePairing)
      }.padding(24)
    }.frame(width: 580, height: 680)
  }
}
