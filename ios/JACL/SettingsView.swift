//  SettingsView.swift
//  The app's Settings sheet, reached from the gear button on the shelf.
//
//  Deliberately minimal: the interpreter has no runtime options on iOS (no
//  sound, no accounts). Its main job is the App-Store-required in-app link to
//  the hosted privacy policy, plus a short About section.

import SwiftUI

struct SettingsView: View {
    @Environment(\.dismiss) private var dismiss

    /// Same policy hosted for the App Store Connect "Privacy Policy URL" field.
    private static let privacyURL = URL(string: "https://jacl.dangarmarine.com.au/privacy.html")!

    private var version: String {
        let info = Bundle.main.infoDictionary
        let short = info?["CFBundleShortVersionString"] as? String ?? "?"
        let build = info?["CFBundleVersion"] as? String ?? "?"
        return "\(short) (\(build))"
    }

    var body: some View {
        NavigationStack {
            Form {
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
