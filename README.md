systemd-cron
================
[systemd][1] units to run [cron][2] scripts

Description
---------------
systemd units to provide cron daemon functionality by running scripts in cron directories.  
The crontabs are automaticaly translated using /usr/lib/systemd/system-generators/[systemd-crontab-generator][6].

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
* systemd ≥ 236
* UsrMerged system
* C and C++20 compilers
* libssl (-lcrypto)
* pkgconf (optional)
* support for /usr/lib/sysusers.d/*.conf (optional)
* [run-parts][3] (optional, disabled by default)
* sendmail from $SENDMAIL or in $PATH or in /usr/sbin:/usr/lib (optional, evaluated at runtime)

Dependencies history
------------------------
* systemd ≥ 197, first support for timers
* systemd ≥ 209, yearly timers
* systemd ≥ 212, persistent timers
* systemd ≥ 217, minutely, quarterly & semi-annually timers
* systemd ≥ 228, `RemainAfterElapse` option, might be usefull for @reboot jobs
* systemd ≥ 229, real random delay support with `RandomizedDelaySec` option [(bug)][90]
* systemd ≥ 233, support for new `~` = 'last day of month' and '9..17/2' .timer syntax
* systemd ≥ 235, support for timezones
* systemd ≥ 236, `LogLevelMax` option
* systemd ≥ 251, OnFailure handler receives $MONITOR_UNIT and more.
* systemd ≥ 255, `SetLoginEnvironment`: no change needed.

Installation
----------------
There exists packages avaible for:
* [Debian][7]
* [Ubuntu][8]
* [Arch][9]
* [Gentoo][10]
* [OpenMamba][11] (RPM based)

There is also a old .spec file for Fedora in [contrib/][12].

A complete list of all packages can be browsed at [Repology][13].

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
* `--datadir=<path>`
* `--libdir=<path>`
* `--libexecdir=<path>`
* `--statedir=<path>`
* `--mandir=<path>`
* `--docdir=<path>`

Other options include (`pkgconf` may be overridden with `$PKG_CONFIG`):

* `--enable-runparts[=yes|no]` Use static units for /etc/cron.{hourly,daily,...}
  Default: `no`.
* `--unitdir=<path>` Path to systemd unit files.
  Default: `pkgconf systemd --variable=systemdsystemunitdir` or `<libdir>/systemd/system`.
* `--generatordir=<path>` Path to systemd generators.
  Default: `pkgconf systemd --variable=systemdsystemgeneratordir` or `<libdir>/systemd/system-generators`.
* `--sysusersdir=<path>` Path to systemd-sysusers snippets.
  Default: `pkgconf systemd --variable=sysusersdir` or `<libdir>/sysusers.d`.
* `--enable-boot[=yes|no]` Include support for the boot timer.
  Default: `yes`.
* `--enable-minutely[=yes|no]` Include support for the minutely timer.
  Default: `no`.
* `--enable-hourly[=yes|no]` Include support for the hourly timer.
  Default: `yes`.
* `--enable-daily[=yes|no]` Include support for the daily timer.
  Default: `yes`.
* `--enable-weekly[=yes|no]` Include support for the weekly timer.
  Default: `yes`.
* `--enable-monthly[=yes|no]` Include support for the monthly timer.
  Default: `yes`.
* `--enable-quarterly[=yes|no]` Include support for the quarterly timer.
  Default: `no`.
* `--enable-semi_annually[=yes|no]` Include support for the semi-annually timer.
  Default: `no`.
* `--enable-yearly[=yes|no]` Include support for the yearly timer.
  Default: `yes`.
* `--libcrypto=<flags>` Compiler and linker flags required to build and link to libcrypto.
  Default: `pkgconf --cflags --libs libcrypto` or `-lcrypto`.
* `--with-part2timer=file` Mapping from basename in /etc/cron.{daily,weekly,etc.) to unit name.
  Default: `/dev/null`.
* `--with-crond2timer=file` Mapping from basename in /etc/cron.d to unit name.
  Default: `/dev/null`.
* `--with-version=ver` Downstream version.
  Default: contents of `VERSION`.

A typical configuration for the latest systemd would be:

    $ ./configure

(the default settings are a common ground between what is seen on current Arch/Debian/Gentoo packaging)

Alternatively you can also generate static .timer/.service to serialize
the jobs in /etc/cron.{hourly,daily,weekly,monthly,...} through run-parts:

    $ ./configure --enable-runparts=yes


`part2timer` and `crond2timer` are in the format

    basename<whitespace>unitbasename[<whitespace>whatever[...

(empty lines and start-of-line `#`-comments permitted; unitbasename doesn't include ".timer") and may be useful when, for example,
`/etc/cron.daily/plocate` has a timer called `plocate-updatedb.timer` or `/etc/cron.d/ntpsec` has a timer called `ntpsec-rotate-stats.timer`:
without the override, the jobs would run twice since native-timer detection would be looking for `plocate.timer` and `ntpsec.timer`.

If there is already a perfect 1:1 mapping between `/etc/cron.<freq>/<job>` and `/usr/lib/systemd/system/<job>.timer`,
then it is not needed to add an entry to these tables.

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
It vendors a derived work of voreutils, which is available under the 0BSD licence.


Copyright
-------------
© 2014, Dwayne Bent : original package with static units  
© 2014, Konstantin Stepanov (me@kstep.me) : author of crontab generator  
© 2014, Daniel Schaal : review of crontab generator  
© 2014-2023, Alexandre Detiste (alexandre@detiste.be) : maintainer  
© 2023, наб (nabijaczleweli@nabijaczleweli.xyz) : rewrite of generator in C++  

[1]: http://www.freedesktop.org/wiki/Software/systemd/ "systemd"
[2]: http://en.wikipedia.org/wiki/Cron "cron"
[3]: https://tracker.debian.org/pkg/debianutils "debianutils"
[4]: https://www.gnu.org/prep/standards/html_node/Directory-Variables.html "Directory Variables"
[5]: http://www.freedesktop.org/software/systemd/man/systemd.timer.html#Persistent= "systemd.timer"
[6]: https://github.com/kstep/systemd-crontab-generator "crontab generator"
[7]: http://packages.debian.org/systemd-cron
[8]: http://packages.ubuntu.com/search?suite=all&searchon=names&keywords=systemd-cron
[9]: https://aur.archlinux.org/packages/systemd-cron
[10]: https://packages.gentoo.org/package/sys-process/systemd-cron
[11]: https://openmamba.org/en/packages/?tag=devel&pkg=systemd-cron.source
[12]: https://github.com/systemd-cron/systemd-cron/blob/master/contrib/systemd-cron.spec
[13]: https://repology.org/project/systemd-cron/packages

[90]: https://bugs.freedesktop.org/show_bug.cgi?id=82084
