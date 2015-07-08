#!/usr/bin/python3
import sys
import os
import subprocess
from subprocess import Popen, PIPE

try:
    job = sys.argv[1]
except IndexError:
    sys.exit("Usage: %s <unit>" % sys.argv[0])

for pgm in ('/usr/sbin/sendmail', '/usr/lib/sendmail'):
    if os.path.exists(pgm):
        break
else:
    print("<3>can't send error mail for %s without a MTA" % job)
    exit(0)

user = subprocess.check_output(['systemctl','show',job,'--property=User'], universal_newlines=True)
user = user.rstrip('\n').split('=')[1]
if not user: user = 'root'

mailto = user

job_env = subprocess.check_output(['systemctl','show',job,'--property=Environment'], universal_newlines=True)
job_env = job_env.rstrip('\n').split('=', 1)[1]

if job_env:
    for var in job_env.split(' '):
        try:
            key , value = var.split('=', 1)
            if key == 'MAILTO': mailto = value
        except ValueError:
            pass

if not mailto: exit(0)

hostname = os.uname()[1]

body = "From: root (systemd-cron)\n"
body += "To: " + mailto + "\n"
body += "Subject: [" + hostname + "] job " + job  + " failed\n"
body += "MIME-Version: 1.0\n"
body += "Content-Type: text/plain; charset=UTF-8\n"
body += "Content-Transfer-Encoding: 8bit\n"
body += "Auto-Submitted: auto-generated\n"
body += "\n"

try:
    systemctl = subprocess.check_output(['systemctl','status',job], universal_newlines=True)
except subprocess.CalledProcessError as e:
    if e.returncode != 3:
        raise
    else:
        body += e.output

p = Popen(['sendmail','-i','-B8BITMIME',mailto], stdin=PIPE)
p.communicate(bytes(body, 'UTF-8'))
