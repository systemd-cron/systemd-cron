Name:           systemd-cron
Version:        1.5.3
Release:        1
License:        MIT
Summary:        systemd units to provide cron daemon & anacron functionality
Url:            https://github.com/systemd-cron/systemd-cron/
Group:          System Environment/Base
Source:         https://github.com/systemd-cron/systemd-cron/archive/v%{version}.tar.gz
Provides:       cronie
Provides:       cronie-anacron
Conflicts:      cronie
Conflicts:      cronie-anacron
BuildArch:      noarch
BuildRoot:      %{_tmppath}/%{name}-%{version}-build
Requires:       crontabs

%description
Provides systemd units to run cron jobs in /etc/cron.hourly cron.daily
cron.weekly and cron.monthly directories, without having cron
or anacron installed.
It also provides a generator that dynamicaly translate /etc/crontab,
/etc/cron.d/* and user cronjobs in systemd units.

%preun
%systemd_preun cron.target

%post
%systemd_post cron.target

%postun
%systemd_postun_with_restart cron.target

%prep
%setup -q

%build
./configure --enable-persistent=yes --prefix=/usr --confdir=/etc --enable-boot=no
make

%install
make DESTDIR=$RPM_BUILD_ROOT install
mkdir -p $RPM_BUILD_ROOT/var/spool/cron
mkdir -p $RPM_BUILD_ROOT/etc/cron.d/
mkdir -p $RPM_BUILD_ROOT/etc/cron.weekly/
cat << EOF > $RPM_BUILD_ROOT/etc/cron.weekly/systemd-cron
#!/bin/sh
test -x /usr/bin/python3 || exit 0
test -x /lib/systemd-cron/remove_stale_stamps || exit 0
exec /lib/systemd-cron/remove_stale_stamps
EOF

%files
%license LICENSE
%doc README.md CHANGELOG
/etc/cron.d/
/etc/cron.weekly/
/usr/bin/crontab
/usr/lib/systemd-cron/mail_on_failure
/usr/lib/systemd-cron/boot_delay
/usr/lib/systemd-cron/remove_stale_stamps
/usr/lib/systemd/system/cron.target
/usr/lib/systemd/system/cron-weekly.service
/usr/lib/systemd/system/cron-update.path
/usr/lib/systemd/system/cron-monthly.timer
/usr/lib/systemd/system/cron-hourly.target
/usr/lib/systemd/system/cron-weekly.timer
/usr/lib/systemd/system/cron-monthly.service
/usr/lib/systemd/system/cron-weekly.target
/usr/lib/systemd/system/cron-failure@.service
/usr/lib/systemd/system/cron-daily.timer
/usr/lib/systemd/system/cron-daily.service
/usr/lib/systemd/system/cron-daily.target
/usr/lib/systemd/system/cron-hourly.service
/usr/lib/systemd/system/cron-update.service
/usr/lib/systemd/system/cron-hourly.timer
/usr/lib/systemd/system/cron-monthly.target
/usr/lib/systemd/system-generators/systemd-crontab-generator
/usr/share/man/man5/crontab.5.gz
/usr/share/man/man5/anacrontab.5.gz
/usr/share/man/man7/systemd.cron.7.gz
/usr/share/man/man1/crontab.1.gz
/usr/share/man/man8/systemd-crontab-generator.8.gz
/var/spool/cron
