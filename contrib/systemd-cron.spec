# Copyright (C) 2015-2025 Alexandre Detiste (alexandre@detiste.be)
# Copyright (C) 2025 Josef Johansson (josef@oderland.se)
# SPDX-License-Identifier: MIT

# https://fedoraproject.org/wiki/Packaging:ScriptletSnippets#Systemd
# https://github.com/systemd/systemd/blob/42911a567dc22c3115fb3ee3c56a7dcfb034f102/src/core/macros.systemd.in

# "If your package includes one or more systemd units that need
# to be enabled by default on package installation,
# they must be covered by the Fedora preset policy."

Name:           systemd-cron
Version:        2.5.1
Release:        1
License:        MIT
Summary:        systemd units to provide cron daemon & anacron functionality
Url:            https://github.com/systemd-cron/systemd-cron/
Source:         https://github.com/systemd-cron/systemd-cron/archive/v%{version}.tar.gz
BuildRequires:  g++
BuildRequires:  libmd-devel
BuildRequires:  systemd-rpm-macros
Provides:       cronie
Provides:       cronie-anacron
Conflicts:      cronie
Conflicts:      cronie-anacron
BuildRoot:      %{_tmppath}/%{name}-%{version}-build
Requires:       crontabs
Requires:       systemd

%description
Provides systemd units to run cron jobs in /etc/cron.hourly cron.daily
cron.weekly and cron.monthly directories, without having cron
or anacron installed.
It also provides a generator that dynamically translate /etc/crontab,
/etc/cron.d/* and user cronjobs in systemd units.

%pre
touch %{_rundir}/crond.reboot

%preun
%systemd_preun cron.target

%post
# XXX this macro doesn't seems to do anything
%systemd_post cron.target
if [ $1 -eq 1 ] ; then
	systemctl daemon-reload
	systemctl enable cron.target
	systemctl start cron.target
fi

%postun
%systemd_postun_with_restart cron.target

%prep
%setup -q

%build
./configure \
  --enable-boot=no \
  --enable-runparts

%make_build

%install
make DESTDIR=$RPM_BUILD_ROOT install
sed -i '/Persistent=true/d' $RPM_BUILD_ROOT/usr/lib/systemd/system/cron-hourly.timer
mkdir -p $RPM_BUILD_ROOT/var/spool/cron
mkdir -p $RPM_BUILD_ROOT/etc/cron.d/
mkdir -p $RPM_BUILD_ROOT/etc/cron.weekly/
cp contrib/systemd-cron.cron.weekly $RPM_BUILD_ROOT/etc/cron.weekly/systemd-cron
mkdir -p $RPM_BUILD_ROOT/usr/lib/systemd/system-preset/
echo 'enable cron.target' > $RPM_BUILD_ROOT/usr/lib/systemd/system-preset/50-systemd-cron.preset

%files
%license LICENSE
%doc README.md CHANGELOG
%dir %{_sysconfdir}/cron.d
/etc/cron.weekly/
%{_bindir}/crontab
%dir /usr/libexec/systemd-cron/
%{_libexecdir}/systemd-cron/mail_for_job
%{_libexecdir}/systemd-cron/boot_delay
%{_libexecdir}/systemd-cron/remove_stale_stamps
%{_libexecdir}/systemd-cron/crontab_setgid
%{_presetdir}/50-systemd-cron.preset
%{_unitdir}/cron.target
%{_unitdir}/cron-weekly.service
%{_unitdir}/cron-update.path
%{_unitdir}/cron-monthly.timer
%{_unitdir}/cron-hourly.target
%{_unitdir}/cron-weekly.timer
%{_unitdir}/cron-monthly.service
%{_unitdir}/cron-weekly.target
%{_unitdir}/cron-mail@.service
%{_unitdir}/cron-daily.timer
%{_unitdir}/cron-daily.service
%{_unitdir}/cron-daily.target
%{_unitdir}/cron-hourly.service
%{_unitdir}/cron-update.service
%{_unitdir}/cron-hourly.timer
%{_unitdir}/cron-monthly.target
%{_unitdir}/cron-yearly.service
%{_unitdir}/cron-yearly.target
%{_unitdir}/cron-yearly.timer
%{_systemdgeneratordir}/systemd-crontab-generator
%{_sysusersdir}/systemd-cron.conf

%{_mandir}/man1/crontab.*
%{_mandir}/man5/crontab.*
%{_mandir}/man5/anacrontab.*
%{_mandir}/man7/systemd.cron.*
%{_mandir}/man8/systemd-crontab-generator.*
%dir /var/spool/cron

%changelog
%autochangelog
