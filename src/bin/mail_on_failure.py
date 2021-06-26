#!/usr/bin/python3
import argparse
import logging
import os
import subprocess
from subprocess import Popen, PIPE

__DOC__ = """ send a panic email about a failed cron job """

parser = argparse.ArgumentParser(description=__DOC__)
parser.add_argument('unit', help='the failing unit, e.g. cron-foo-1.service')
parser.add_argument('--verbose', action='store_true')
args = parser.parse_args()
if args.verbose:
    logging.getLogger().setLevel(logging.INFO)

for pgm in ('/usr/sbin/sendmail', '/usr/lib/sendmail'):
    if os.path.exists(pgm):
        break
else:
    print("<3>can't send error mail for %s without a MTA" % args.unit)
    exit(0)

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

if job_env:
    for var in job_env.split(' '):
        try:
            key , value = var.split('=', 1)
            if key == 'MAILTO':
                mailto = value
        except ValueError:
            pass

if not mailto:
   logging.info('This cron job (%s) opted out of email, therefore quitting', args.unit)
   exit(0)

hostname = os.uname()[1]

body = "From: root (systemd-cron)\n"
body += "To: " + mailto + "\n"
body += "Subject: [" + hostname + "] job " + args.unit  + " failed\n"
body += "MIME-Version: 1.0\n"
body += "Content-Type: text/plain; charset=UTF-8\n"
body += "Content-Transfer-Encoding: 8bit\n"
body += "Auto-Submitted: auto-generated\n"
body += "\n"

for locale in (None, 'C.UTF-8', 'C'):
    if locale:
        os.environ['LC_ALL'] = locale
    try:
        subprocess.check_output(['systemctl', 'status', args.unit], universal_newlines=True)
    except UnicodeDecodeError:
        logging.info('current locale (%s) is broken, try again', locale)
    except subprocess.CalledProcessError as e:
        if e.returncode != 3:
            raise
        else:
            body += e.output
            break

p = Popen(['sendmail','-i','-B8BITMIME',mailto], stdin=PIPE)
p.communicate(bytes(body, 'UTF-8'))
