#!/bin/bash
JOB_ENV=$(systemctl cat $1 | grep ^Environment)
MAILTO=$(echo $JOB_ENV | sed -r 's/.*MAILTO=([^"]*).*/\1/')

[ -z "$MAILTO" ] && MAILTO=root

cat <<EOF | sendmail -i -B8BITMIME "$MAILTO"
Subject: [$HOSTNAME] job $1 failed
MIME-Version: 1.0
Content-Type: text/plain; charset=UTF-8
Content-Transfer-Encoding: 8bit

$(systemctl status "$1")
EOF