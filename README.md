systemd-cron
================
[systemd][1] units to run [cron][2] scripts

Description
---------------
systemd units to provide cron daemon functionality by running scripts in cron directories.  
The crontabs are automaticaly translated using (/usr)/lib/systemd/system-generators/[systemd-crontab-generator][6].

Usage
---------
Add executable scripts to the appropriate cron directory (e.g. `/etc/cron.daily`) and enable systemd-cron:

    # systemctl daemon-reload
    # systemctl enable cron.target
    # systemctl start cron.target

The project also includes simple crontab command equivalent, which behaves like standard crontab command (and accepts the same main options).
   
The scripts should now be automatically run by systemd. See man:systemd.cron(7) for more information.

Dependencies
----------------
* systemd ≥ 197
    * systemd ≥ 209, yearly timers
    * systemd ≥ 212, persistent timers
    * systemd ≥ 217, minutely , quarterly & semi-annually timers
    * systemd ≥ 229, real random delay support with `RandomizedDelaySec` option
* [run-parts][3]
* python 3
* /usr/sbin/sendmail (optional, evaluated at runtime)
* gcc or clang (needed to build `crontab_setgid.c`; this helper is optional and evaluated at runtime)

Installation
----------------
There exists packages avaible for:
* [Debian][7]
* [Ubuntu][8]
* [Arch][9]
* [Gentoo][10]

There is also a .spec file for Fedora in [contrib/][11].

You can also build it manually from source.


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
* `--runparts=<path>` The path installations should use for the `run-parts` executable.
  Default: `<prefix>/bin/run-parts`.
* `--enable-boot[=yes|no]` Include support for the boot timer.
  Default: `yes`.
* `--enable-minutely[=yes|no]` Include support for the minutely timer. Requires systemd ≥ 217.
  Default: `no`.
* `--enable-hourly[=yes|no]` Include support for the hourly timer.
  Default: `yes`.
* `--enable-daily[=yes|no]` Include support for the daily timer.
  Default: `yes`.
* `--enable-weekly[=yes|no]` Include support for the weekly timer.
  Default: `yes`.
* `--enable-monthly[=yes|no]` Include support for the monthly timer.
  Default: `yes`.
* `--enable-quarterly[=yes|no]` Include support for the quarterly timer. Requires systemd ≥ 217.
  Default: `no`.
* `--enable-semi_annually[=yes|no]` Include support for the semi-annually timer. Requires systemd ≥ 217.
  Default: `no`.
* `--enable-yearly[=yes|no]` Include support for the yearly timer. Requires systemd ≥ 209.
  Default: `no`.
* `--enable-persistent[=yes|no]` Make timers [persistent][5]. Requires systemd ≥ 212.
  Default: `no`.
* `--enable-randomized-delay=[yes|no]` Use [`RandomizedDelaySec`] option for `RANDOM_DELAY` support. Requires systemd ≥ 229.
  Default: `yes`.
* `--enable-setgid[=yes|no]` Compile setgid C helper for crontab. Needs GCC or Clang.
  Default: `no`.

A typical configuration for the latest systemd would be:

    $ ./configure --prefix=/usr --confdir=/etc --enable-yearly --enable-persistent

If you only want the generator (you'll have to provide your own `/etc/crontab` to drive /etc/cron.daily/ etc...):

    $ ./configure --enable-boot=no --enable-hourly=no --enable-daily=no --enable-weekly=no --enable-month=no --enable-persistent --prefix=/usr --confdir=/etc

### Caveat

Your package should also run these extra commands before starting cron.target
to ensure that @reboot scripts doesn't trigger right away:

    # touch /run/crond.reboot
    # touch /run/crond.bootdir

See Also
------------
`systemd.cron(7)` or in source tree `man -l src/man/systemd.cron.7`


License
-----------
The project is licensed under MIT.


Copyright
-------------
© 2014, Dwayne Bent : original package with static units  
© 2014, Konstantin Stepanov (me@kstep.me) : author of crontab generator  
© 2014, Daniel Schaal : review of crontab generator  
© 2014, Alexandre Detiste (alexandre@detiste.be) : manpage for crontab generator  


[1]: http://www.freedesktop.org/wiki/Software/systemd/ "systemd"
[2]: http://en.wikipedia.org/wiki/Cron "cron"
[3]: http://packages.qa.debian.org/d/debianutils.html "debianutils"
[4]: https://www.gnu.org/prep/standards/html_node/Directory-Variables.html "Directory Variables"
[5]: http://www.freedesktop.org/software/systemd/man/systemd.timer.html#Persistent= "systemd.timer"
[6]: https://github.com/kstep/systemd-crontab-generator "crontab generator"
[7]: http://packages.debian.org/systemd-cron
[8]: http://packages.ubuntu.com/search?suite=all&searchon=names&keywords=systemd-cron
[9]: https://aur.archlinux.org/packages/systemd-cron
[10]: https://packages.gentoo.org/package/sys-process/systemd-cron
[11]: https://github.com/systemd-cron/systemd-cron/blob/master/contrib/systemd-cron.spec
