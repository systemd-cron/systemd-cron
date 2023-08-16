#!/bin/sh

set -e
set -u

# previously the {daily/weekly/monthly/..} stamps
# were special-cased: they were to be purged only
# once at 'systemd-cron' removal

# now with the transition to run-part-less mode,
# these become extraneous cruft too and
# can/should be removed if the matching .timer
# does not exist anymore

find /var/lib/systemd/timers/ -name 'stamp-cron-*' -type f -mtime +10 | while read -r stamp
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
