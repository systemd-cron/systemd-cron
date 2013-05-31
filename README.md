systemd-cron
================
[systemd][1] units to run [cron][2] scripts

Description
---------------
systemd units to provide minimal cron daemon functionality by running scripts
in cron directories.

Dependencies
----------------
* systemd ≥ 197
* [run-parts][3]

Packaging
--------------
Building

    $ ./configure
    $ make

Staging

    $ make DESTDIR="$destdir" install

See `configure` for configuration variables. At the very least, you
probably want to override `prefix` and `confdir`, e.g.:

    $ ./configure --prefix=/usr --confdir=/etc

See Also
------------
`system.cron(7)` or in source tree `man -l src/man/systemd.cron.7`

[1]: http://www.freedesktop.org/wiki/Software/systemd/ "systemd"
[2]: http://en.wikipedia.org/wiki/Cron "cron"
[3]: http://packages.qa.debian.org/d/debianutils.html "debianutils"

