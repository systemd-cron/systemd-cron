#!/bin/bash
JOB_ENV=$(systemctl cat $1 | grep ^Environment)
MAILTO=$(echo $JOB_ENV | sed -r 's/.*MAILTO=([^"]*).*/\1/')

[ -z "$MAILTO" ] && MAILTO=root

cat <<EOF | sendmail -i -B8BITMIME "$MAILTO"
Subject: [$HOSTNAME] job $1 failed

$(systemctl status "$1")
EOF
