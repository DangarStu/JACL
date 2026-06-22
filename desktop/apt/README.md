# JACL desktop — APT repository (Debian / Ubuntu)

A signed APT repo is published to GitHub Pages on every `desktop-v*` release, so
Linux users can install and auto-update with `apt`.

## Install

```bash
# 1. trust the repo's signing key
curl -fsSL https://apt.dangarmarine.com.au/jacl.gpg | sudo gpg --dearmor -o /usr/share/keyrings/jacl.gpg

# 2. add the repo
echo "deb [signed-by=/usr/share/keyrings/jacl.gpg] https://apt.dangarmarine.com.au stable main" \
  | sudo tee /etc/apt/sources.list.d/jacl.list

# 3. install
sudo apt update && sudo apt install jacl-desktop
```

`apt upgrade` then picks up new releases automatically.

> amd64 only (Debian/Ubuntu family). On any other Linux, the **`.AppImage`** on the
> [Releases page](https://github.com/DangarStu/JACL/releases) runs with no install.

## How it works / maintenance

- `.github/workflows/desktop.yml` → the **`apt-repo`** job runs after the Linux build
  on a `desktop-v*` tag. It adds the new `.deb` to `pool/main/` on the `gh-pages`
  branch (keeping older versions), regenerates `dists/stable/…` (`Packages`,
  `Release`), and GPG-signs `InRelease` + `Release.gpg`.
- **Signing key:** private key in the `APT_GPG_PRIVATE_KEY` repo secret (no
  passphrase); public key is **`desktop/apt/jacl.gpg`** (committed) and is copied to
  the Pages root as `jacl.gpg` for users to import. Fingerprint
  `50EBC4C2EBD182EF36AA21AD37874D532601DA28`.
- **One-time setup:** after the first `desktop-v*` build creates the `gh-pages`
  branch, enable Pages: repo **Settings → Pages → Deploy from a branch → `gh-pages` /
  (root)**. (Until then the URL 404s.)
</content>
