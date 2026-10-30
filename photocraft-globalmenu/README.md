# PhotoCraft with KDE Plasma global menu

[PhotoCraft](https://github.com/storytold/photocraft) 0.17.0 built from source plus
`kde-global-menu.patch`, which puts its menus (File … Help) in Plasma's Global Menu applet
instead of drawing them inside the window. Replaces `photocraft-bin`.

PhotoCraft is egui, so its menu bar is painted pixels that Plasma can't import. Upstream already
has a platform-neutral native menu layer for the macOS menu bar
(`crates/ui-egui/src/menus/native_menu.rs`); the patch adds a Linux backend for it:

- **`apps/photocraft/src/dbus_menu.rs`** serves the menus as `com.canonical.dbusmenu` (what Qt and
  GTK apps use) with zbus, which upstream already depends on. Clicks run the command like an
  in-window click; enabled/checked/labels update live; shortcuts are shown in the menu.
- **Window link:** native Wayland uses KDE's `org_kde_kwin_appmenu` protocol
  (`crates/kde-appmenu`, a tiny isolated `unsafe` crate like upstream's `crates/tablet`, since it
  has to reuse winit's `wl_display`). Xwayland/X11 sets `_KDE_NET_WM_APPMENU_*` window properties.
- **Same menus as in the window:** `desktop_layout` keeps the in-window menus, translations and
  mnemonics (`_File`, `_Edit`, …). Alt+letter or F10 in the window opens that menu in the panel.
- Only kicks in when `com.canonical.AppMenu.Registrar` is on the session bus (Plasma with a
  global menu), the same check Qt does. Elsewhere, or if anything fails, the in-window menus stay.
  `--in-window-menus` / `PHOTOCRAFT_IN_WINDOW_MENUS=1` forces them.

The in-window bar's workspace switcher, search and theme buttons stay where they were.

## Build

```sh
makepkg -si
```

Big Rust build (release profile: thin LTO, one codegen unit), and the final link wants several
GB of RAM. Close heavy stuff first, or cap parallelism: `CARGO_BUILD_JOBS=8 makepkg -si`.

## Updating to a new PhotoCraft release

The patch is generated from a git checkout in `dev/` (ignored by this repo), where the changes
sit uncommitted on top of the release tag:

```sh
cd dev
git stash -u && git fetch --depth 1 origin tag v<new> && git checkout v<new> && git stash pop
cargo test -p photocraft-ui-egui --lib native_menu && cargo test -p photocraft --bin photocraft dbus_menu
git add -N . && git diff v<new> > ../kde-global-menu.patch
```

Then bump `pkgver` (and `_fontsref` if upstream's `release.yml` moved it), `updpkgsums`,
`makepkg --printsrcinfo > .SRCINFO`.

## Known limits

- If the Global Menu applet is removed while PhotoCraft runs, its menus are gone until a restart
  (Qt apps behave the same).
- Help › Search's inline search field doesn't exist in the global menu; Plasma's applet has its
  own search, and Edit › Search still works.
