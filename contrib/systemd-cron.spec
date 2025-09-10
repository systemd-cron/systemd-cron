# SPDX-License-Identifier: MIT
# Copyright (C) 2015-2025 Alexandre Detiste (alexandre@detiste.be)
# Copyright (C) 2025 Josef Johansson (josef@oderland.se)
# Copyright (C) 2025 Pramod (pramodvu1502@proton.me)

Name:           systemd-cron
Version:        2.5.1
Release:        %{autorelease}
License:        MIT
Summary:        systemd generator to provide cron daemon & anacron functionality
Url:            https://github.com/systemd-cron/systemd-cron
Source0:        %{url}/archive/v%{version}.tar.gz
BuildRequires:  g++
BuildRequires:  libmd-devel
BuildRequires:  systemd-rpm-macros
Conflicts:      cronie
Conflicts:      cronie-anacron
Requires:       crontabs
Requires:       systemd
Requires:       libmd

%description
A systemd generator implementing (ana)cron.
It uses systemd.timer and systemd.service under the hood.

%pre
# Prevent execution of boot and reboot scripts on installation
touch %{_rundir}/crond.reboot %{_rundir}/crond.bootdir

%preun
%systemd_preun cron.target

%post
%systemd_post cron.target

# To apply the /var/spool/cron and crontab group to the system on installation
%{_bindir}/systemctl reload-or-restart systemd-sysusers.service systemd-tmpfiles-setup.service

%postun
%systemd_postun_with_restart cron.target

%prep
%setup -q

%build
# Don't use the %%configure macro as this is not an autotool script
./configure \
  --version="%{version}" \
  --prefix="%{_prefix}" \
  --bindir="%{_bindir}" \
  --datadir="%{_datadir}" \
  --libdir="%{_libdir}" \
  --libexecdir="%{_libexecdir}" \
  --mandir="%{_mandir}" \
  --docdir="%{_docdir}" \
  --unitdir="%{_unitdir}" \
  --generatordir="%{_systemdgeneratordir}" \
  --sysusersdir="%{_sysusersdir}" \
  --enable-runparts=no \
  --enable-minutely=yes \
  --enable-quarterly=yes \
  --enable-semi_annually=yes \
  --enable-boot=no

# Disabled run-parts i.e. static single units for each granuarity level.
# Instead opting to use only the systemd-crontab-generator
# run-parts override the generator when installed
#
# Also enabled all disabled-by-default granularity levels
#
# cron.boot isn't otherwise supported in fedora, we follow that.

# Compile
%make_build

%install
%make_install

# Systemd preset policy
mkdir -p "${RPM_BUILD_ROOT}%{_presetdir}"
echo 'enable cron.target' > "${RPM_BUILD_ROOT}%{_presetdir}/50-systemd-cron.preset"

# Preparing /var/spool/cron using tmpfiles.d
mkdir -p "${RPM_BUILD_ROOT}%{_tmpfilesdir}"
echo "d %{_localstatedir}/spool/cron 1730 root crontab" >> "${RPM_BUILD_ROOT}%{_tmpfilesdir}/systemd-cron.conf"

# setgid helper
chmod g+s "${RPM_BUILD_ROOT}%{_libexecdir}/systemd-cron/crontab_setgid"

%files
%license LICENSE
%doc README.md CHANGELOG

%dir %{_sysconfdir}/cron.d
%dir %{_libexecdir}/systemd-cron

%{_bindir}/crontab
%{_libexecdir}/systemd-cron/mail_for_job
%{_libexecdir}/systemd-cron/boot_delay
%{_libexecdir}/systemd-cron/remove_stale_stamps
%attr(2755, root, crontab) %{_libexecdir}/systemd-cron/crontab_setgid
%{_systemdgeneratordir}/systemd-crontab-generator

%{_presetdir}/50-systemd-cron.preset
%{_unitdir}/cron.target
%{_unitdir}/cron-update.path
%{_unitdir}/cron-update.service
%{_unitdir}/cron-mail@.service
%{_unitdir}/systemd-cron-cleaner.service
%{_unitdir}/systemd-cron-cleaner.timer

%{_sysusersdir}/systemd-cron.conf
%{_tmpfilesdir}/systemd-cron.conf

%{_mandir}/man1/crontab.*
%{_mandir}/man5/crontab.*
%{_mandir}/man5/anacrontab.*
%{_mandir}/man7/systemd.cron.*
%{_mandir}/man8/systemd-crontab-generator.*

%changelog
%autochangelog
