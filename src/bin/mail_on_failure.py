#!/usr/bin/python3
import argparse
import email.mime.text
import email.utils
import logging
import os
import shlex
import subprocess


__DOC__ = """ send a panic email about a failed cron job """

parser = argparse.ArgumentParser(description=__DOC__)
parser.add_argument('unit', help='the failing unit, e.g. cron-foo-1.service')
parser.add_argument('--verbose', action='store_true')
args = parser.parse_args()
if args.verbose:
    logging.getLogger().setLevel(logging.INFO)

# NOTE: this used to check for /usr/lib/sendmail, but
#       ConditionFileIsExecutable=/usr/sbin/sendmail
#       in cron-failure@ meant that was unreachable.

user = subprocess.check_output(
    ['systemctl', 'show', args.unit, '--property=User'],
    universal_newlines=True)
user = user.rstrip('\n').split('=')[1]
if not user:
    user = 'root'
mailto = user

job_env = subprocess.check_output(
    ['systemctl', 'show', args.unit, '--property=Environment'],
    universal_newlines=True)
job_env = job_env.rstrip('\n').split('=', 1)[1]

# FIXME: don't try to re-parse systemctl show's Environment=.
#        shlex isn't 100% consistent with systemd.
#        Get structured data from systemd.
#        But how -- dbus? -o json?
#        Probably adds a dependency on python3-systemd :-(
if job_env:
    for pair in shlex.split(job_env):
        if pair.startswith('MAILTO='):
            mailto = pair[len('MAILTO='):]

if not mailto:
    logging.info('This cron job (%s) opted out of email, therefore quitting', args.unit)
    exit(0)

# FIXME: are any of these "better"?
#          platform.uname().node
#          socket.gethostname()
#          socket.getfqdn()
hostname = os.uname().nodename  # NOTE: requires Python 3.3+

for locale in (None, 'C.UTF-8', 'C'):
    if locale:
        # FIXME: pass a deep copy of os.environ to check_output,
        #        instead of editing the version used for the rest of this process.
        os.environ['LC_ALL'] = locale
    try:
        subprocess.check_output(
            ['systemctl', 'status', args.unit, '--full'],
            universal_newlines=True)
    except UnicodeDecodeError:
        logging.info('current locale (%s) is broken, try again', locale)
        pass
    except subprocess.CalledProcessError as e:
        if e.returncode != 3:   # FIXME: what is this magic number?
            raise
        else:
            # Convert stdout into an email.
            # FIXME: capture stderr also?
            message_object = email.mime.text.MIMEText(_text=e.output)
            break
    raise RuntimeError('systemctl status should have failed')


message_object['Date'] = email.utils.formatdate()
message_object['From'] = 'root (systemd-cron)'
message_object['To'] = mailto
message_object['Subject'] = f'[{hostname}] job {args.unit} failed'  # NOTE: requires Python 3.6+
message_object['Auto-Submitted'] = 'auto-generated'  # https://datatracker.ietf.org/doc/html/rfc3834#section-5

subprocess.run(
        ['/usr/sbin/sendmail',  # for consistency with ConditionFileIsExecutable=
         '-i',
         '-B8BITMIME',
         mailto],                 # recipient
        check=True,               # like subprocess.check_call()
        universal_newlines=False,  # os.environ['LC_ALL'] gets wrecked above
        input=message_object.as_bytes())
