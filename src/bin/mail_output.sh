#!/bin/sh

# helper wrapper for people who wants systemd-cron
# to behave closer to vixie-cron

unit="$1"
shift

SENDMAIL="$(command -v "$SENDMAIL" || command -v sendmail || command -v /usr/sbin/sendmail || command -v /usr/lib/sendmail)" || {
	# there's no point to wrap the process without a sendmail
	exec "$@"
	# exec may fail if program doesn't exist,
        # let well-tested mail_on_failure.sh handle this.
	exit 1
}

output="$(mktemp -t "systemd-cron.XXXXXXXXX")"


# shellcheck disable=SC2068
# SC2068 (error): Double quote array expansions to avoid re-splitting elements
$@ > "$output" 2>&1
rc=$?

if [ $rc = 0 ] && [ ! -s "$output" ]
then
	# everthing was fine, no mail to send
	rm "$output"
	exit 0
fi

systemctl show --property=User --property=Environment "$unit" | {
	user=
	job_env=
	while IFS='=' read -r k v; do
		case "$k" in
			'User'       )	user="$v"    ;;
			'Environment')	job_env="$v" ;;
		esac
	done
	[ -z "$user" ] && user='root'

	mailto="$user"
	mailfrom='root'

	for kv in $job_env; do
		case "$kv" in
			'MAILTO='*  )	mailto="${kv#'MAILTO='}"     ;;
			'MAILFROM='*)	mailfrom="${kv#'MAILFROM='}" ;;
		esac
	done

	[ -z "$mailto" ] && {
		[ -n "$verbose" ] && printf 'This cron job (%s) opted out of email, therefore quitting\n' "$unit" >&2
		exit 0
	}

	{
		# Encode the message in raw 8-bit UTF-8. w/o base64
		# Virtually all modern MTAs are 8-bit clean and send each other 8-bit data
		# without checking each other's 8BITMIME flag.
		if [ $rc = 0 ]
		then
			msg="output"
		else
			msg="failed"
		fi

		printf '%s: %s\n' 'Content-Type'              'text/plain; charset="utf-8"'    \
		                  'MIME-Version'              '1.0'                            \
		                  'Content-Transfer-Encoding' 'binary'                         \
		                  'Date'                      "$(date -R)"                     \
		                  'From'                      "$mailfrom (systemd-cron)"       \
		                  'To'                        "$mailto"                        \
		                  'Subject'                   "[$(uname -n)] job $unit $msg"   \
		                  'X-Mailer'                  "systemd-cron @version@"         \
		                  'Auto-Submitted'            'auto-generated'
		# https://datatracker.ietf.org/doc/html/rfc3834#section-5

		cat "$output"
		exit 0
	} 2>&1 | "$SENDMAIL" -i -B 8BITMIME "$mailto"
}
