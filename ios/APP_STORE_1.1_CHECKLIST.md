# JACL iOS 1.1 — App Store resubmission checklist

1.0 (iPad-only) is approved and live. 1.1 ships everything since: iPhone support,
column reading layout, landscape margins, transcript scroll fix, build-info line.
Because it now supports iPhone, 1.1 is a **universal app** — the listing needs
iPhone screenshots and "iPhone and iPad" wording.

## 0. Pre-flight cleanup (do FIRST — don't ship the test game)
- [ ] Delete the bundled test game so it can't end up in the archive:
      - `rm ios/StarterGames/scrolltest.jaclgame`
      - `rm android/app/src/main/assets/scrolltest.jaclgame`
      - remove their two lines from `.gitignore`
- [ ] `cd ios && xcodegen generate` (so the project no longer references it)
- [ ] Confirm `ls ios/StarterGames/*.jaclgame` shows only the 6 real starters
- [ ] (Leave `projects/scrolltest.jacl` — it's `game_publish false`, never ships)

## 1. Version bump
- [ ] In `ios/project.yml` (the source of truth — Info.plist is generated):
      - `CFBundleShortVersionString: "1.1"`  (was 1.0)
      - `CFBundleVersion: "2"`               (was 1)
- [ ] `cd ios && xcodegen generate`

## 2. iPhone screenshots (NEW — Apple requires them now that iPhone is supported)
Capture the same three scenes as iPad — **Bookshelf, Map, Settings** — at iPhone
size. The required size is the **6.9" iPhone** (Apple accepts it for all iPhone
sizes, like 12.9" covers all iPads):
- [ ] **6.9" iPhone**: 1320 × 2868 px portrait (iPhone 16 Pro Max).
      (1290 × 2796, the 6.7" 15 Pro Max size, is also accepted.)
- [ ] Easiest: run an **iPhone 16 Pro Max simulator**, drive it, `xcrun simctl io
      booted screenshot` each scene. (Claude can generate these — just ask.)
- [ ] iPad 12.9" shots already exist from 1.0 — reuse them.

## 3. Store-listing edits (App Store Connect → 1.1)
- [ ] Description: change "iPad" → "iPhone and iPad" (the line "Designed for iPad,
      in portrait or landscape" → "Designed for iPhone and iPad…").
- [ ] Subtitle / promotional text: drop any "for iPad" wording.
- [ ] Add the iPhone screenshots; iPad screenshots stay.
- [ ] Compatibility/devices updates automatically from the binary (family 1,2).

## 4. "What's New in This Version" (release notes)
> Now on iPhone as well as iPad. A more comfortable read: text scales to a column
> width you choose, with generous margins in landscape, and the transcript now
> scrolls so each command's response starts from the top — a screenful at a time.

## 5. Archive → upload → submit (same flow as 1.0)
- [ ] Archive via CLI with the paid team (never commit the team id):
      `xcodebuild -project ios/JACL.xcodeproj -scheme JACL -configuration Release
       -archivePath build/JACL.xcarchive archive DEVELOPMENT_TEAM=5TU3TU28JN`
- [ ] Open the archive in **Xcode → Organizer**, Distribute App → App Store Connect → Upload
- [ ] In App Store Connect: create the **1.1** version, attach the build, paste the
      release notes, confirm export-compliance (already false), **Submit for Review**

## Notes
- Apple team: **5TU3TU28JN** (paid Individual). No login needed for review (no accounts).
- The bundle id is unchanged (`au.com.dangarmarine.JACL`); this is an update, not a new app.
- Android Play is a separate track (1.0.1 landscape fix currently in review).
