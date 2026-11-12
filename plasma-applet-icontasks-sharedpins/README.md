# Icons-Only Task Manager (Shared Pins)

A separate Plasma widget, `org.archbuilds.icontasks`: the Icons-Only Task Manager from
`plasma-desktop` 6.8.2 with one extra setting, so a task manager can use the pinned apps of another
one. It installs next to the stock task managers and leaves `plasma-desktop` alone.

- **Setting.** Behavior → "Pinned apps:". "This widget's own" is the default; every other task
  manager in plasmashell (this widget, the stock Icons-Only Task Manager and the stock Task Manager)
  is listed as "Same as …", with its panel, screen and number of pinned apps.
- **Stays in sync.** Pinning, unpinning and moving apps in either widget changes both, also when
  the other one is a stock task manager. Chains work (A follows B, B follows C: all share C's list).
- **Own list is kept as a copy.** Choosing another widget replaces this widget's list with that
  widget's. Going back to "This widget's own", or removing the other widget, leaves the last
  shared list in place.
- Picks the other widget up when it's loaded later, e.g. a panel on a screen plugged in after
  plasmashell started.

Everything else is the stock widget's code and settings, and it uses the stock widget's
translations (the new strings stay in English).

## How it's built

`prepare()` takes `applets/taskmanager` from the official `plasma-desktop` tarball, plus the
settings file it borrows from `kcms/recentFiles`, then applies `icontasks-sharedpins.patch` and
adds a standalone `CMakeLists.txt` and `metadata.json`. The patch:

- adds `launcherssource.{h,cpp}` (finds the other task managers, resolves which one to follow) and
  the `launchersSource` setting (the other widget's id, 0 for the own list), and wires both into
  `main.qml` and `ConfigBehavior.qml`;
- renames the widget id and QML module, and puts the C++ classes in the `IconTasksSharedPins`
  namespace, so the plugin can't clash with the stock one when both are loaded.

## Install

```sh
gpg --import keys/pgp/*.asc   # once, so makepkg can verify the source signature
makepkg -si
systemctl --user restart plasma-plasmashell
```

Then add "Icons-Only Task Manager (Shared Pins)" to a panel (Add or Manage Widgets), or right-click
an existing Icons-Only Task Manager → Show Alternatives. Swapping it in through Show Alternatives
keeps the pinned apps.

## Notes

- **Rebuild after every Plasma update.** The widget links to plasma-workspace's private libraries
  (`libtaskmanager`, `libnotificationmanager`) and QML modules, which have no stable ABI. Set
  `pkgver` to the new Plasma version, update the tarball checksum, and check the patch still applies.
- **Global shortcuts** (Meta+1…9, Meta+Ctrl+PgUp/PgDn, …) keep their stock names, so the ones you
  set up carry over. With a stock task manager loaded in plasmashell at the same time, both
  register them, so it's best to replace the stock widgets rather than mix the two.
