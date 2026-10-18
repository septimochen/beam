import SwiftUI

struct BeamView: View {
  @ObservedObject var model: DesktopModel
  @State private var page = "Share"

  var body: some View {
    VStack(spacing: 0) {
      Picker("Workspace", selection: $page) {
        Text("Share").tag("Share")
        Text("Devices").tag("Devices")
      }.pickerStyle(.segmented).frame(width: 240).padding(.top, 16)
      if page == "Devices" { DevicesView(model: model) } else { shareView }
    }.frame(minWidth: 680, minHeight: 640)
      .sheet(isPresented: $model.showPairing, onDismiss: model.closePairing) {
        PairingView(model: model)
      }
  }

  private var shareView: some View {
    ScrollView {
      VStack(alignment: .leading, spacing: 20) {
        HStack(spacing: 12) {
          Image(systemName: "arrow.up.arrow.down.circle.fill")
            .font(.system(size: 38)).foregroundStyle(.tint)
            .accessibilityHidden(true)
          VStack(alignment: .leading, spacing: 4) {
            Text("Beam").font(.largeTitle.bold())
            Text("Share directly with your paired devices.")
              .foregroundStyle(.secondary)
          }
        }
        GroupBox("Send") {
          VStack(alignment: .leading, spacing: 12) {
            HStack {
              Picker("Device", selection: $model.peer) {
                if model.peers.isEmpty { Text("No paired devices").tag("") }
                ForEach(model.peers, id: \.self) { Text($0).tag($0) }
              }
              Button("Manage devices") { page = "Devices" }
              Button("Add device…", action: model.openPairing)
                .disabled(model.sending || model.checking)
            }
            HStack {
              Button("Check connectivity", action: model.checkConnectivity)
                .disabled(model.peers.isEmpty || model.checking)
              Text(model.connectivityStatus).textSelection(.enabled)
            }
            Picker("Content type", selection: $model.kind) {
              ForEach(ContentSelection.allCases) { Text($0.rawValue).tag($0) }
            }.pickerStyle(.segmented)
            HStack {
              Button("Choose file…", action: model.chooseFile)
              Button("Load clipboard text") { model.loadClipboard() }
            }
            if model.kind == .file {
              Text(model.file.isEmpty ? "Choose a file or drop it into this window" : model.file)
                .foregroundStyle(model.file.isEmpty ? .secondary : .primary)
                .textSelection(.enabled)
                .accessibilityLabel("Selected file: \(model.file)")
            } else {
              HStack {
                Text("Text to send")
                Spacer()
                Button("Copy to clipboard") { model.copyText(model.content) }
              }
              ReviewTextEditor(model: model)
                .font(.body.monospaced()).frame(height: 100)
                .accessibilityLabel("Content to send")
                .overlay(RoundedRectangle(cornerRadius: 6).stroke(.quaternary))
            }
            Text(
              "Clipboard text is read only when you click Load. Nothing is sent until you click Send."
            )
            .font(.caption).foregroundStyle(.secondary)
            HStack {
              Button("Send", action: model.send)
                .buttonStyle(.borderedProminent)
                .disabled(model.sending || model.peers.isEmpty)
              Button("Cancel send") { model.bridge.cancelSend() }
                .disabled(!model.sending)
            }
            transferProgress(model.sendProgress, label: "Send progress")
            Text(model.sendStatus).textSelection(.enabled)
          }.padding(8).frame(maxWidth: .infinity, alignment: .leading)
        }
        GroupBox("Receive") {
          VStack(alignment: .leading, spacing: 12) {
            HStack {
              Text(model.output.isEmpty ? "Choose where received content is saved" : model.output)
                .foregroundStyle(model.output.isEmpty ? .secondary : .primary)
                .textSelection(.enabled)
                .accessibilityLabel("Receive folder: \(model.output)")
              Spacer()
              Button("Choose folder…", action: model.chooseFolder)
            }
            transferProgress(model.receiveProgress, label: "Receive progress")
            Text(model.receiveStatus).textSelection(.enabled)
            if let text = model.receivedText {
              VStack(alignment: .leading) {
                HStack {
                  Text("Received text").font(.headline)
                  Spacer()
                  Button("Copy to clipboard") { model.copyText(text) }
                }
                ScrollView {
                  Text(text).font(.body.monospaced()).textSelection(.enabled)
                    .frame(maxWidth: .infinity, alignment: .leading)
                }.frame(height: 120).accessibilityLabel("Received text")
              }
            }
          }.padding(8).frame(maxWidth: .infinity, alignment: .leading)
        }
        Text(model.lastResult).font(.callout).textSelection(.enabled)
        Text("Use Add device to pair over your local network.")
          .font(.caption).foregroundStyle(.secondary)
      }.padding(24)
    }
  }

  @ViewBuilder private func transferProgress(_ value: Double?, label: String) -> some View {
    if let value {
      ProgressView(value: value).accessibilityLabel(label)
    } else {
      ProgressView().controlSize(.small).accessibilityLabel(label)
    }
  }
}
