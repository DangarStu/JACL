//  SettingsView.swift
//  The app's Settings sheet, reached from the gear button on the shelf.
//
//  Deliberately minimal: the interpreter has no runtime options on iOS (no
//  sound, no accounts). It offers a "Get more games" link to the website's
//  iPad downloads, the App-Store-required privacy-policy link, and a short
//  About section. These are plain Links that hand off to Safari -- the app
//  itself makes no network connections.

import SwiftUI

struct SettingsView: View {
    @Environment(\.dismiss) private var dismiss
    @AppStorage(ReadingDefaults.columnsKey) private var columns = ReadingDefaults.columns
    @AppStorage(ReadingDefaults.marginsKey) private var margins: MarginWidth = ReadingDefaults.defaultMargins
    @AppStorage(AppearanceMode.key) private var appearance = AppearanceMode.system
    @AppStorage("soundEnabled") private var soundEnabled = true

    /// Opens the site's "Get it for iPad" downloads tab directly -- the #get
    /// fragment selects that tab. A plain Safari hand-off; no in-app network.
    private static let getMoreURL = URL(string: "https://jacl.dangarmarine.com.au/#get")!

    /// Same policy hosted for the App Store Connect "Privacy Policy URL" field.
    private static let privacyURL = URL(string: "https://jacl.dangarmarine.com.au/privacy.html")!

    private var version: String {
        let info = Bundle.main.infoDictionary
        let short = info?["CFBundleShortVersionString"] as? String ?? "?"
        let build = info?["CFBundleVersion"] as? String ?? "?"
        var s = "\(short) (\(build))"
        // Append the binary's build time so it's clear which build is installed.
        if let exec = Bundle.main.executableURL,
           let date = (try? FileManager.default.attributesOfItem(atPath: exec.path))?[.modificationDate] as? Date {
            let f = DateFormatter()
            f.dateFormat = "yyyy-MM-dd HH:mm"
            s += " · \(f.string(from: date))"
        }
        return s
    }

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    VStack(alignment: .leading, spacing: 10) {
                        HStack {
                            Text("Columns")
                            Spacer()
                            Text("\(Int(columns))").foregroundStyle(.secondary)
                        }
                        HStack(spacing: 12) {
                            Image(systemName: "textformat.size.larger")
                                .foregroundStyle(.secondary)
                            Slider(value: $columns,
                                   in: ReadingDefaults.columnRange, step: 1)
                                .accessibilityLabel("Columns")
                            Image(systemName: "textformat.size.smaller")
                                .foregroundStyle(.secondary)
                        }
                        Text("Characters per line. Fewer columns means larger text; the font scales with the window.")
                            .font(.footnote)
                            .foregroundStyle(.secondary)
                            .lineLimit(2)
                    }
                    .padding(.vertical, 4)

                    Picker("Margins", selection: $margins) {
                        ForEach(MarginWidth.allCases) { Text($0.label).tag($0) }
                    }
                    .pickerStyle(.segmented)

                    Picker("Appearance", selection: $appearance) {
                        ForEach(AppearanceMode.allCases) { Text($0.label).tag($0) }
                    }
                } header: {
                    Text("Reading")
                } footer: {
                    Text("Line width and how much side margin to keep; the font "
                       + "scales with the window to satisfy both. Appearance "
                       + "\u{201C}System\u{201D} follows your device's Light/Dark "
                       + "setting; the shelf stays dark.")
                }

                Section {
                    Toggle("Sound", isOn: $soundEnabled)
                } footer: {
                    Text("Play game sound effects and music. Applies to every game.")
                }

                Section {
                    Link(destination: Self.getMoreURL) {
                        Label("Get more games", systemImage: "arrow.down.circle")
                    }
                } footer: {
                    Text("Browse the iPad games on jacl.dangarmarine.com.au. Tap "
                       + "a game to download it, then choose \u{201C}Open in "
                       + "JACL\u{201D} to install it.")
                }

                Section {
                    Link(destination: Self.privacyURL) {
                        Label("Privacy Policy", systemImage: "hand.raised")
                    }
                } footer: {
                    Text("JACL collects no personal data. Games and saved games "
                       + "stay on your iPad; the app makes no network connections "
                       + "and has no accounts or sign-in.")
                }

                Section("About") {
                    LabeledContent("Version", value: version)
                    LabeledContent("Interpreter", value: "JACL by Stuart Allen")
                    Link(destination: URL(string: "https://jacl.dangarmarine.com.au/")!) {
                        Label("jacl.dangarmarine.com.au", systemImage: "globe")
                    }
                }
            }
            .navigationTitle("Settings")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                }
            }
        }
    }
}
