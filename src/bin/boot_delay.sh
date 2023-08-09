#!/bin/sh
if ! { [ $# -eq 1 ] && delay=$(( $1 * 60 )); }; then
	printf 'Usage: %s minutes\n' "$0"
	exit 1
fi

IFS=".$IFS" read -r uptime _ < /proc/uptime
[ "$uptime" -ge "$delay" ] || exec sleep $(( delay - uptime ))
