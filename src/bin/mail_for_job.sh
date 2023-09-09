#!/bin/sh -f

[ $# -eq 1 ] || {
	printf 'usage: %s unit[:Success|:Failure][:nonempty][:nometadata][:verbose]\n' "$0" >&2
	exit 1
}
unit="$1"

verbose=
metadata=m
nonempty=
while :; do
	case "$unit" in
		*:verbose   )	verbose=v;  unit="${unit%:*}" ;;
		*:nonempty  )	nonempty=n; unit="${unit%:*}" ;;
		*:nometadata)	metadata=;  unit="${unit%:*}" ;;
		*:[A-Z]*    )	            unit="${unit%:*}" ;;  # https://github.com/systemd-cron/systemd-cron/issues/120
		*           )	break                         ;;
	esac
done


SENDMAIL="$(command -v "$SENDMAIL" || command -v sendmail || command -v /usr/sbin/sendmail || command -v /usr/lib/sendmail)" || {
	printf '<3>can'\''t send mail for %s without a MTA\n' "$unit"
	exit 0
}

systemctl show --property=User --property=Environment --property=ActiveState --property=InvocationID "$unit" | {
	user=
	job_env=
	active_state=
	invocation_id=
	while IFS='=' read -r k v; do
		case "$k" in
			'User'        )	user="$v"          ;;
			'Environment' )	job_env="$v"       ;;
			'ActiveState' )	active_state="$v"  ;;
			'InvocationID')	invocation_id="$v" ;;
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

	[ -n "$nonempty" ] && {
		# INVOCATION_ID=          matches messages from systemd
		# _SYSTEMD_INVOCATION_ID= matches messages from the service
		journalctl -qu "$unit" _SYSTEMD_INVOCATION_ID="$invocation_id" | read -r _ || {
			[ -n "$verbose" ] && printf 'This cron job (%s) produced no output, therefore quitting\n' "$unit" >&2
			exit 0
		}
	}

	[ "$active_state" = 'failed' ] && why='failed' || why='output'  # "[tarta] job cron-whatever failed" or "[tarta] job cron-whatever output"
	{
		# Encode the message in raw 8-bit UTF-8. w/o base64
		# Virtually all modern MTAs are 8-bit clean and send each other 8-bit data
		# without checking each other's 8BITMIME flag.

		printf '%s: %s\n' 'Content-Type'              'text/plain; charset="utf-8"'  \
		                  'MIME-Version'              '1.0'                          \
		                  'Content-Transfer-Encoding' 'binary'                       \
		                  'Date'                      "$(date -R)"                   \
		                  'From'                      "$mailfrom (systemd-cron)"     \
		                  'To'                        "$mailto"                      \
		                  'Subject'                   "[$(uname -n)] job $unit $why" \
		                  'X-Mailer'                  "systemd-cron @version@"       \
		                  'Auto-Submitted'            'auto-generated'
		# https://datatracker.ietf.org/doc/html/rfc3834#section-5

		echo
		case "${LC_ALL-"${LC_CTYPE-"${LANG}"}"}" in
			C|POSIX|*.utf8|*.utf-8|*.UTF-8)	               ;;  # ok, we can safely use this locale as UTF-8
			*                             )	LC_ALL=C.UTF-8 ;;  # forced to comply with charset=
		esac

		if [ -n "$metadata" ]; then
			systemctl status -n0 "$unit"
			journalctl -u "$unit" -o short-iso _SYSTEMD_INVOCATION_ID="$invocation_id" + INVOCATION_ID="$invocation_id"
		else
			journalctl -u "$unit" -o cat       _SYSTEMD_INVOCATION_ID="$invocation_id"
		fi
	} 2>&1 | "$SENDMAIL" -i -B 8BITMIME "$mailto"
}
