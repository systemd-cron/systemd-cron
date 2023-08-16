#!/bin/sh
set -e
set -u

find /var/lib/systemd/timers/ -name 'stamp-cron-*' -mtime +10 | while read -r stamp
do
    timer=${stamp##*/stamp-}

    if test -f "/run/systemd/generator/$timer"
    then
        :
    elif test -f "/lib/systemd/system/$timer"
    then
        :
    else
        rm -f "$stamp"
    fi
done

exit 0
