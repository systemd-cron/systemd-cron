systemd-cron
================
[systemd][1] units to run [cron][2] scripts

Description
---------------
systemd units to provide minimal cron daemon functionality by running scripts in cron directories.

Dependencies
----------------
* systemd ≥ 197
    * systemd ≥ 209, yearly timers
    * systemd ≥ 212, persistent timers
* [run-parts][3]

Packaging
--------------

### Building

    $ ./configure
    $ make

### Staging

    $ make DESTDIR="$destdir" install

### Configuration

The `configure` script takes command line arguments to configure various details of the build. The following options
follow the standard GNU [installation directories][4]:

* `--prefix=<path>`
* `--bindir=<path>`
* `--confdir=<path>`
* `--datadir=<path>`
* `--libdir=<path>`
* `--statedir=<path>`
* `--mandir=<path>`
* `--docdir=<path>`

Other options include:

* `--unitdir=<path>` Path to systemd unit files.
  Default: `<libdir>/systemd/system`.
* `--runpaths=<path>` The path installations should use for the `run-parts` executable.
  Default: `<prefix>/bin/run-parts`.
* `--enable-boot[=yes|no]` Include support for the boot timer.
  Default: `yes`.
* `--enable-hourly[=yes|no]` Include support for the hourly timer.
  Default: `yes`.
* `--enable-daily[=yes|no]` Include support for the daily timer.
  Default: `yes`.
* `--enable-weekly[=yes|no]` Include support for the weekly timer.
  Default: `yes`.
* `--enable-monthly[=yes|no]` Include support for the monthly timer.
  Default: `yes`.
* `--enable-yearly[=yes|no]` Include support for the yearly timer. Requires systemd ≥ 209.
  Default: `no`.
* `--enable-persistent[=yes|no]` Make timers [persistent][5]. Requires systemd ≥ 212.
  Default: `no`.

A typical configuration for the latest systemd would be:

    $ ./configure --prefix=/usr --confdir=/etc --enable-yearly --enable-persistent

See Also
------------
`systemd.cron(7)` or in source tree `man -l src/man/systemd.cron.7`

[1]: http://www.freedesktop.org/wiki/Software/systemd/ "systemd"
[2]: http://en.wikipedia.org/wiki/Cron "cron"
[3]: http://packages.qa.debian.org/d/debianutils.html "debianutils"
[4]: https://www.gnu.org/prep/standards/html_node/Directory-Variables.html "Directory Variables"
[5]: http://www.freedesktop.org/software/systemd/man/systemd.timer.html#Persistent= "systemd.timer"

