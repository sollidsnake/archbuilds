# kde-appmenu-gtk-module

GTK 3 module that puts the menu bars of GTK 3 apps (GIMP, …) in Plasma's Global Menu applet, the way
Qt apps do it, **natively on Wayland**. Alt+mnemonic and F10 in the window open the menu in the panel.

Why it's needed: GTK 3 apps don't export their menus on their own. The only route Plasma had was
`appmenu-gtk-module` plus Plasma's `gmenudbusmenuproxy`, and that only works on X11 (the proxy finds
menus through X window properties). On Wayland nothing linked a GTK window to its menu, so GTK apps
either kept the menu in the window, or lost it entirely with `appmenu-gtk-module` loaded. Even on
X11 the keyboard didn't work: the proxy never asks Plasma to open a menu, so Alt+F did nothing.

## How it works

- Every `GtkMenuBar` in a normal window is exported as `com.canonical.dbusmenu` with
  `libdbusmenu-gtk3` (Ubuntu's GTK menu exporter, which keeps it in sync with the widgets: labels,
  sensitivity, checkboxes, icons, items added and removed). The module fills in what its parser misses:
  shortcuts set with `gtk_accel_label_set_accel()`, GtkApplication menus, and GIMP's shortcuts
  (looked up from the app's `GtkApplication` accels), plus the bogus checkboxes on GtkApplication
  menu items.
- The window is linked to the menu the same way Qt does it: KWin's `org_kde_kwin_appmenu` protocol
  on native Wayland (re-sent whenever GTK recreates the window's surface), `_KDE_NET_WM_APPMENU_*`
  window properties on X11/Xwayland.
- While exported, the menu bar takes no space and isn't drawn, but stays mapped, so GTK still routes
  its Alt+mnemonics to it. The module catches those, and F10 (`gtk-menu-bar-accel`, unless the app
  uses F10 as a shortcut), and sends `ItemActivationRequested`, which makes the applet open that
  menu (with the `plasma-workspace` keyboard fixes in this repo: on the right screen, underlines etc.).
- Apps whose menu bar comes from `gtk_application_set_menubar()` get theirs too: Plasma's proxy sets
  `gtk-shell-shows-menubar` in GTK's `settings.ini`, which makes `GtkApplicationWindow` leave the
  menu bar out on X11. The module turns that off in-process so there is a menu bar to export.
- Only active when there is a global menu, i.e. `com.canonical.AppMenu.Registrar` is on the session
  bus (the same check Qt does) and, on Wayland, KWin offers `org_kde_kwin_appmenu_manager`. Otherwise,
  and in windows it doesn't handle (popups, plugs, offscreen windows), menu bars stay in the window.
  Like with Qt, the check happens once when the app starts.

## Install

```sh
makepkg -si
```

The package installs `/usr/lib/environment.d/60-kde-appmenu-gtk-module.conf`, which adds the
module to `GTK_MODULES` for the whole session. **Log out and back in** for it to apply. Before that,
try it on one app:

```sh
GTK_MODULES=kde-appmenu-gtk-module gimp
```

To keep one app's menu bar in the window: `GTK_MODULES= gimp`.

`appmenu-gtk-module` conflicts (both would take the same menu bars). Plasma's proxy keeps listing
it in `~/.config/gtk-3.0/settings.ini`; with it not installed GTK just prints
`Failed to load module "appmenu-gtk-module"`, which is harmless.

## Limitations

- GTK 3 only. GTK 4 has no modules; GTK 4 apps with a real menu bar would need a patched `gtk4`.
- Debug output: `G_MESSAGES_DEBUG=kde-appmenu-gtk-module`.
