# plasma-workspace with global menu keyboard fixes

Arch's `plasma-workspace` 6.8.2 packaging (`pkgrel` set to `1.1` to match CachyOS) plus
`appmenu-keyboard-popup.patch`, which fixes the Global Menu applet (`org.kde.plasma.appmenu`)
when it's used from the keyboard on Wayland:

- **Menu opens only once.** Opening it with the applet's shortcut or an Alt+mnemonic worked the
  first time, then failed ("Failed to create grabbing popup") until the panel was clicked. The
  popup menu had the importer's hidden root menu as parent, and QMenu swapped our transient
  parent (the panel) for that surface-less window.
- **Several applets fight over Alt+mnemonics.** With a Global Menu on more than one panel (menus
  for all screens is the default), every applet opened the app's menu at once. Qt nested the
  popups and the keyboard ended up in the menu on the other screen. Now only the applet on the
  window's screen reacts, or the first one if none is.
- **Stuck in Search.** Left/Right in the empty Search field now switch to the neighbouring menu
  like in every other menu.
- **First open after plasmashell starts.** Before plasmashell got any input, the keyboard couldn't
  open the menu at all. Uses the same input serial workaround as the appmenu kded module.
- **Missing Alt underlines.** Holding Alt often underlined the wrong letter or none at all.
  `Kirigami.MnemonicData` hands out letters from a pool shared by all of plasmashell, where the
  buttons from before the last menu reload and the applets on other panels still held them. The
  buttons now underline the letter the app marked, which is the one it reacts to.
- **Alt brings up a hidden panel (optional, off by default).** Turn on "Show hidden panel after
  holding Alt for" in the applet's settings, with a delay of 0–5000 ms (1000 by default). Holding
  Alt that long then shows an auto-hidden panel with a Global Menu, so you can see the menus and
  their letters. It hides again when Alt is released, unless a menu got opened. Only the panel
  that would open the menu on Alt+letter shows up (the one on the active window's screen), and
  only if the window has a menu.

Also fixes the `Parameter "index" is not declared` QML warning on the mnemonic path.

## Build

```sh
gpg --import keys/pgp/*.asc   # once, so makepkg can verify the source signature
makepkg -si
systemctl --user restart plasma-plasmashell
```

It's a big C++ build. `MAKEFLAGS` from `/etc/makepkg.conf` applies and can't be overridden from
the environment, so lower it there if the machine is short on RAM.

## Updating

Every `plasma-workspace` update from the repos replaces this package. To rebuild for a new
version, take the new Arch PKGBUILD, re-add the patch to `source`/`sha256sums` and the
`prepare()` step, and keep `pkgrel` equal to the repo's so the next real update still wins.
