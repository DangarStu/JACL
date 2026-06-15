# JACL — iOS App Store Submission

Everything needed to submit the iPad app to the App Store. Copy each field
straight into App Store Connect. iOS goes first; Android follows later.

Status legend:  ✅ ready   ⏳ blocked on enrollment   ✏️ you must supply/confirm

---

## SUBMITTED — version 1.0 (build 1)

- **Submitted:** 15 June 2026, "Waiting for Review"
- **Submission ID:** `6be04e7d-d29c-4817-b6db-0cd53c61cf66`
- **Signed with:** Individual team `5TU3TU28JN` (see [[project_apple_signing]])
- Release: automatic on approval (no phased/manual release set).
- If rejected: check the Resolution Center, fix, and resubmit.

---

## 0. Publisher account

- **Decision: enroll an _Individual / Sole Proprietor_ membership** under your
  own Apple ID. Seller name = your legal name. ~A$149/yr.
- Do **not** publish under "Noyce Consulting Pty Limited" — that's Matthew
  Noyce's organization and you only hold a "Developer" role there (can't submit,
  wrong public seller name, renewal/ownership risk).
- Being a member of the Noyce team does **not** block your own Individual
  enrollment. Leave that association as-is.
- Enroll via the **Apple Developer** app → Account → Enroll → Individual
  (fastest ID check), or https://developer.apple.com/programs/enroll/
- ⏳ Verification typically clears in 24–48h. Everything below is ready to go
  the moment it does.

---

## 1. App information

| Field | Value |
|---|---|
| Bundle ID | `au.com.dangarmarine.JACL` |
| Version (CFBundleShortVersionString) | `1.0` |
| Build (CFBundleVersion) | `1` |
| Platform / device | iPad only (TARGETED_DEVICE_FAMILY = 2) |
| Min iOS | 17.0 |
| Encryption | `ITSAppUsesNonExemptEncryption = false` (export-compliance question auto-skipped) |

---

## 2. Listing text

### App Name  (max 30)
`JACL — Text Adventures`   *(alt: `JACL Interactive Fiction`)*

### Subtitle  (max 30)
`Classic text adventure games`

### Keywords  (max 100, comma-separated, no spaces)
`interactive fiction,text adventure,IF,parser game,adventure,retro,80s,gamebook,story,reader`

### Promotional Text  (max 170, editable any time without review)
`A pocket library of text adventures — type what you want to do and the story responds. Explore, map your world, and pick up right where you left off.`

### Description  (max 4000)

```
JACL brings the golden age of text adventures to your iPad. There are no
buttons to mash and no twitch reflexes required — you read where you are, type
what you want to do ("open the door", "take the lantern", "go north"), and the
story responds. It's interactive fiction the way it was meant to be: you and
your imagination, one sentence at a time.

The app comes with a shelf of ready-to-play adventures spanning several
languages and moods — from a fantasy dungeon to a quiet stroll through Paris —
so there's something to play the moment you open it.

FEATURES

• A built-in bookshelf of adventures, ready to play offline
• Never lose your place — every game auto-saves, so returning to the shelf and
  coming back drops you exactly where you left off
• Named save points for marking the moments you want to return to
• A tap-to-open map that charts the rooms you've explored
• Sound and music in games that support it
• Tap and hold any word to look it up in the dictionary
• Comfortable, adjustable serif reading — set your text size once and it sticks
• Light, Dark, or System appearance for the reading screen
• Designed for iPad, in portrait or landscape

WHO IT'S FOR

If you grew up with text adventures in the 1980s, this is your nostalgia
machine. If you've never played one, it's the gentlest possible introduction to
a whole genre of story-driven games. The bundled adventures range from
family-friendly puzzles to mature, tongue-in-cheek action, so there's room to
grow into.

More adventures can be downloaded and opened directly in the app.

Curl up with a story. Type your way through it.
```

### What's New (version 1.0)
```
First release. A shelf of text adventures, auto-save and resume, an explorable
map, sound support, and adjustable serif reading — all built for iPad.
```

---

## 3. URLs

| Field | Value | Status |
|---|---|---|
| Privacy Policy URL | `https://jacl.dangarmarine.com.au/privacy.html` | ✅ live (HTTP 200), byte-identical to `etc/privacy.html`, already linked from both apps |
| Support URL (required) | `https://jacl.dangarmarine.com.au/` (or the GitHub repo) | ✏️ confirm this is acceptable as a support/contact page |
| Marketing URL (optional) | `https://jacl.dangarmarine.com.au/` | optional |

---

## 4. App Privacy (Data Collection questionnaire)

- **Answer: "Data Not Collected."** ✏️ Confirm still true: the app stores
  games/saves/settings on-device only, has no accounts/sign-in, no analytics, no
  ads, and makes no network calls home. (This matches the published privacy
  policy.)

---

## 5. Age rating

- **17+.** Driven by the bundled "Bloody Guns" (mild/infrequent mature &
  suggestive themes + cartoon/fantasy violence). Answer the questionnaire to
  produce 17+. You already accepted this implication (older audience, 80s
  nostalgia).

---

## 6. Category & price

- Primary category: **Games → Adventure** (alt: Games → Role Playing)
- Secondary (optional): Entertainment, or Books
- Price: **Free** (set tier in Pricing and Availability) — ✏️ confirm.

---

## 7. Screenshots  (12.9" iPad, 2064 × 2752)

Captured and ready:

| Shot | File |
|---|---|
| Bookshelf / game library | `/tmp/ss_shelf.png` |
| In-game map (Bloody Guns, current room highlighted) | `/tmp/ss_map.png` |
| Settings (text size, appearance, sound) | `/tmp/ss_settings.png` |

> Move these somewhere permanent before submitting (they're in /tmp). App Store
> Connect accepts 12.9" iPad shots for all iPad sizes.

---

## 8. Archive & upload (run once enrollment clears)

A shared `JACL` scheme and `ios/ExportOptions.plist` are already in place.

```bash
cd ios
xcodegen generate

# Archive (Release). Use your Individual team ID — likely the same personal team
# (5TU3TU28JN) after it upgrades, but check the Membership page.
xcodebuild -project JACL.xcodeproj -scheme JACL -configuration Release \
  -sdk iphoneos -archivePath build/JACL.xcarchive \
  DEVELOPMENT_TEAM=<your-team-id> -allowProvisioningUpdates archive
```

Then upload, either:

- **Easiest (recommended for a first submission):** open `build/JACL.xcarchive`
  in **Xcode → Window → Organizer → Distribute App** and follow the wizard
  (handles signing + upload), **or**
- **CLI:** set `<teamID>` in `ios/ExportOptions.plist`, then
  ```bash
  xcodebuild -exportArchive -archivePath build/JACL.xcarchive \
    -exportPath build/export -exportOptionsPlist ExportOptions.plist \
    -allowProvisioningUpdates
  ```
  and upload `build/export/*.ipa` with Transporter or `xcrun altool`.

Apple processes the build (~15–60 min); it then becomes selectable in App Store
Connect.

---

## 9. Submission checklist

- [ ] ⏳ Individual Apple Developer membership active
- [ ] Create app record in App Store Connect (bundle `au.com.dangarmarine.JACL`)
- [ ] Paste listing text (§2)
- [ ] Set URLs (§3) — privacy ✅, ✏️ confirm support
- [ ] App Privacy = Data Not Collected (§4)
- [ ] Age rating questionnaire → 17+ (§5)
- [ ] Category + price (§6)
- [ ] Upload screenshots (§7)
- [ ] Archive + upload build (§8), select it on the version
- [ ] Submit for review (export compliance auto-skipped)

Review is typically 1–3 days.

---

## 10. Android (later)

Same listing copy and privacy URL reuse for Google Play. Play wants its own
Data Safety form (mirror "no data collected"), a feature graphic, and phone +
tablet screenshots. Tackle after iOS is live.
```
