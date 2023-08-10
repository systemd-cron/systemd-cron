#!/bin/sh
LC_ALL=C
TZ=UTC0
export LC_ALL TZ

trap 'rm -rf "$tmpdir"' EXIT INT
tmpdir="$(mktemp -dt "remove_stale_stamps.XXXXXXXXX")/"

printf '%s\n' /var/lib/systemd/timers/stamp-cron-*.timer > "${tmpdir}actual_stamps"

printf '%s\n' /var/lib/systemd/timers/stamp-cron-daily.timer \
              /var/lib/systemd/timers/stamp-cron-weekly.timer \
              /var/lib/systemd/timers/stamp-cron-monthly.timer \
              /var/lib/systemd/timers/stamp-cron-quarterly.timer \
              /var/lib/systemd/timers/stamp-cron-semi-annually.timer \
              /var/lib/systemd/timers/stamp-cron-yearly.timer \
              /run/systemd/generator/cron-*.timer |
	sed 's:/run/systemd/generator/cron-:/var/lib/systemd/timers/stamp-cron-:' |
	sort > "${tmpdir}needed_stamps"

now="$(date +%s)"

comm -23 "${tmpdir}actual_stamps" "${tmpdir}needed_stamps" | while read -r stamp; do
	mtime="$(stat -c'%Y' "$stamp")"
	[ "$mtime" -lt $(( now - 10 * 86400 )) ] && rm -f "$stamp" 2>/dev/null
done
:
