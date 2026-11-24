# Secret Mod Buttons

A Geode mod for **Geometry Dash 2.2091** (`Geode v6.0.0-beta.2`) that unlocks the hidden Moderator and Elder Moderator buttons in `LevelInfoLayer`:

- **Mod Send / Rate (`mod-send-btn`)**: Opens `RateStarsLayer` in moderator mode with Star/Moon sliders and Feature / Epic / Legendary / Mythic toggles.
- **Mod Demon Rate (`mod-demon-btn`)**: Opens `RateDemonLayer` to preview the Demon difficulty rating popup.
- **Elder Mod Delete (`elder-delete-btn`)**: Restores the hidden Elder Moderator delete button on the left side menu.

> **Note:** All menus are authentic client-side Geometry Dash UI layers, while RobTop's servers still enforce account-level moderator checks when submitting requests.

## Building with GitHub Actions

Push this repository to GitHub (`main` branch). The workflow in `.github/workflows/build.yml` automatically compiles `.geode` packages for **Windows (`Win64`), Android (`Android64` / `Android32`), macOS (`MacOS`), and iOS (`iOS`)** against **Geode `v6.0.0-beta.2`** and uploads them under the **Actions** tab artifacts.

## Building Locally

```bash
geode build
```
