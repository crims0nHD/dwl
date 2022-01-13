# dawus - David's awesome wayland using software

## Disclaimer
This project is based on [djpohly/dwl](https://github.com/djpohly/dwl) and serves as a learning resource for me.

## Goals
My goals for this project are:
- understanding what dwl does
- implementing some quality of life features
- lua configuration and runtime interop for a lua client AwesomeWM style? (This might be a bit ambiguous but hey this project is mainly for educational purposes anyway :) )

## Building dwl

dwl has only two dependencies: wlroots and wayland-protocols. Simply install these (and their `-devel` versions if your distro has separate development packages) and run `make`.  If you wish to build against a Git version of wlroots, check out the [wlroots-next branch](https://github.com/djpohly/dwl/tree/wlroots-next).

To enable XWayland, you should also install xorg-xwayland and uncomment its flag in `config.mk`.

## Configuration

All configuration is done by editing `config.h` and recompiling, in the same manner as dwm. There is no way to separately restart the window manager in Wayland without restarting the entire display server, so any changes will take effect the next time dwl is executed.

As in the dwm community, we encourage users to share patches they have created.  Check out the [patches page on our wiki](https://github.com/djpohly/dwl/wiki/Patches)!

## Running dwl

dwl can be run on any of the backends supported by wlroots. This means you can run it as a separate window inside either an X11 or Wayland session, as well as directly from a VT console. Depending on your distro's setup, you may need to add your user to the `video` and `input` groups before you can run dwl on a VT.

When dwl is run with no arguments, it will launch the server and begin handling any shortcuts configured in `config.h`. There is no status bar or other decoration initially; these are instead clients that can be run within the Wayland session.

If you would like to run a script or command automatically at startup, you can specify the command using the `-s` option. This command will be executed as a shell command using `/bin/sh -c`.  It serves a similar function to `.xinitrc`, but differs in that the display server will not shut down when this process terminates. Instead, dwl will send this process a SIGTERM at shutdown and wait for it to terminate (if it hasn't already). This makes it ideal for execing into a user service manager like [s6](https://skarnet.org/software/s6/), [anopa](https://jjacky.com/anopa/), [runit](http://smarden.org/runit/faq.html#userservices), or [`systemd --user`](https://wiki.archlinux.org/title/Systemd/User).

Note: The `-s` command is run as a *child process* of dwl, which means that it does not have the ability to affect the environment of dwl or of any processes that it spawns. If you need to set environment variables that affect the entire dwl session, these must be set prior to running dwl.  For example, Wayland requires a valid `XDG_RUNTIME_DIR`, which is usually set up by a session manager such as `elogind` or `systemd-logind`.  If your system doesn't do this automatically, you will need to configure it prior to launching `dwl`, e.g.:

    export XDG_RUNTIME_DIR=/tmp/xdg-runtime-$(id -u)
    mkdir -p $XDG_RUNTIME_DIR
    dwl

### Status information

Information about selected layouts, current window title, and selected/occupied/urgent tags is written to the stdin of the `-s` command (see the `printstatus()` function for details).  This information can be used to populate an external status bar with a script that parses the information.  Failing to read this information will cause dwl to block, so if you do want to run a startup command that does not consume the status information, you can close standard input with the `<&-` shell redirection, for example:

    dwl -s 'foot --server <&-'

If your startup command is a shell script, you can achieve the same inside the script with the line

    exec <&-

## Replacements for X applications

You can find a [list of Wayland applications on the sway wiki](https://github.com/swaywm/sway/wiki/i3-Migration-Guide).

## IRC channel

dwl's IRC channel is #dwl on irc.freenode.net.

## Acknowledgements

dwl began by extending the TinyWL example provided (CC0) by the sway/wlroots developers. This was made possible in many cases by looking at how sway accomplished something, then trying to do the same in as suckless a way as possible.

Many thanks to suckless.org and the dwm developers and community for the inspiration, and to the various contributors to the project, including:

- Alexander Courtis for the XWayland implementation
- Guido Cella for the layer-shell protocol implementation, patch maintenance, and for helping to keep the project running
- Stivvo for output management and fullscreen support, and patch maintenance
