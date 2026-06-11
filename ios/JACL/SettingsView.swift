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

    /// Opens the site's "Get it for iPad" downloads tab directly -- the #get
    /// fragment selects that tab. A plain Safari hand-off; no in-app network.
    private static let getMoreURL = URL(string: "https://jacl.dangarmarine.com.au/#get")!

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
