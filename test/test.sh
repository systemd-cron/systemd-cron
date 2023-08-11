#!/bin/sh
# expects to run as root of a mountns
S_C_G="$PWD/out/build/bin/systemd-crontab-generator"
# S_C_G="./systemd-crontab-generator.py"
PATH="/usr/bin:$PATH"

mount -t tmpfs tmpfs /etc || exit

echo 'dummy:x:12:34:dummy:/home/dummy:/bin/sh' > /etc/passwd

printf '%s\n' '# test_path_expansion, test_period_basic' \
              '@daily dummy true'                        \
                                                         \
              '# test_userpath_expansion'                \
              '@daily dummy ~/fake'                      \
                                                         \
              '# test_timespec_basic'                    \
              '5 6 * * * dummy true'                     \
                                                         \
              '# test_timespec_slice'                    \
              '*/5 * * * * dummy true'                   \
                                                         \
              '# test_timespec_range'                    \
              '1 * * * mon-wed dummy true' > /etc/crontab

"$S_C_G" /etc/out
cd /etc/out       || exit
err=0


assert_key() {  # file k v testname
	val="$(grep "^$2=" "$1")"
	[ "${val#"$2="}" = "$3" ] || { err=1; echo "$4: $val != $3" >&2; }
}

assert_dat() {  # file content testname
	printf '%s\n' "$2" | cmp -s - "$1" || { err=1; echo "$3: $(cat "$1") != $2" >&2; }
}


assert_key cron-crontab-dummy-24edba7d1c50729bdba1f91f44e293f9.service 'ExecStart'  '/usr/bin/true' test_path_expansion
assert_key cron-crontab-dummy-24edba7d1c50729bdba1f91f44e293f9.timer   'OnCalendar' 'daily'         test_period_basic

assert_key cron-crontab-dummy-00e11d1caaf3c28c9ca5766d8483ae83.service 'ExecStart'        '/bin/sh /etc/out/cron-crontab-dummy-00e11d1caaf3c28c9ca5766d8483ae83.sh' test_userpath_expansion
assert_dat cron-crontab-dummy-00e11d1caaf3c28c9ca5766d8483ae83.sh      '/home/dummy/fake'                                                                           test_userpath_expansion

assert_key cron-crontab-dummy-0.timer   'OnCalendar' '*-*-* 6:5:00'                                 test_timespec_basic
assert_key cron-crontab-dummy-1.timer   'OnCalendar' '*-*-* *:0,5,10,15,20,25,30,35,40,45,50,55:00' test_timespec_slice
assert_key cron-crontab-dummy-2.timer   'OnCalendar' 'Mon,Tue,Wed *-*-* *:1:00'                     test_timespec_range

exit $err
