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

id="$(id -u)"
if [ "$id" = 0 ]
then
    stampdir='/var/lib/systemd/timers/'
    rundir='/run/systemd/generator'
    libdir='/lib/systemd/system'
else
    stampdir="$HOME/.local/share/systemd/timers/"
    rundir="/run/user/$id/systemd/generator"
    libdir='/n/a'
fi

find "$stampdir" -name 'stamp-cron-*' -type f -mtime +10 | while read -r stamp
do
    timer=${stamp##*/stamp-}

    if test -f "$rundir/$timer"
    then
        :
    elif test -f "$libdir/$timer"
    then
        :
    else
        rm -f "$stamp"
    fi
done

exit 0
