# GRID0-cfw

the switch side of GRID0. sysmodule + toolbox app, one repo.

- `sys-grid0/` ZeroTier ported to the switch as a sysmodule.
- `toolbox/` the GRID0+ toolbox homebrew app.

CI (`.github/workflows/build.yml`) builds only the side that changed and takes the other from the latest release, then packs one SD zip. A new toolbox version releases as `v<version>`; a sysmodule-only change releases as `v<version>-sys.<run>`.
