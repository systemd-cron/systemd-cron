#!/usr/bin/python3
import argparse
import email.mime.text
import email.utils
import logging
import os
import subprocess

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
user = user.rstrip('\n')
user = user.split('=')[1]
if not user:
    user = 'root'

mailto = user
mailfrom = 'root'

job_env = subprocess.check_output(
                        ['systemctl', 'show', args.unit, '--property=Environment'],
                        universal_newlines=True)
job_env = job_env.rstrip('\n')

if job_env:
    for var in job_env.split('=', 1)[1].split(' '):
        try:
            key , value = var.split('=', 1)
            if key == 'MAILTO':
                mailto = value
            if key == 'MAILFROM':
                mailfrom = value
        except ValueError:
            pass

if not mailto:
   logging.info('This cron job (%s) opted out of email, therefore quitting', args.unit)
   exit(0)

hostname = os.uname()[1]

for locale in (None, 'C.UTF-8', 'C'):
    if locale:
        os.environ['LC_ALL'] = locale
    try:
        output = subprocess.check_output(['systemctl', 'status', args.unit], universal_newlines=True)
        logging.warning('systemctl status should have failed')
        break
    except UnicodeDecodeError:
        logging.info('current locale (%s) is broken, try again', locale)
    except subprocess.CalledProcessError as e:
        if e.returncode != 3:
            raise
        else:
            output = e.output
            break

# Encode the message in 8-bit UTF-8
# Virtually all modern MTAs are 8-bit clean and send each other 8-bit data
# without checking each other's 8BITMIME flag.
utf8_8bit = email.charset.Charset('utf-8')
utf8_8bit.body_encoding = None

message_object = email.mime.text.MIMEText(_text=output, _charset=utf8_8bit)
message_object['Date'] = email.utils.formatdate()
message_object['From'] = mailfrom + ' (systemd-cron)'
message_object['To'] = mailto
message_object['Subject'] = "[" + hostname + "] job " + args.unit + " failed"
# https://datatracker.ietf.org/doc/html/rfc3834#section-5
message_object['Auto-Submitted'] = 'auto-generated'

subprocess.run(
        ['sendmail',
         '-i',
         '-B8BITMIME',
         mailto],
        universal_newlines=False,
        input=message_object.as_bytes())
