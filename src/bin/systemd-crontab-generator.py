#!/usr/bin/python3
import copy
import errno
import hashlib
import os
import pwd
import re
import stat
import string
import sys
from functools import reduce
from typing import Iterator, Optional
from enum import IntEnum

ENVVAR_RE = re.compile(r'^([A-Za-z_0-9]+)\s*=\s*(.*)$')

MINUTES_SET = list(range(0, 60))
HOURS_SET = list(range(0, 24))
DAYS_SET = list(range(1, 32))
DOWS_SET = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun']
MONTHS_SET = list(range(1, 13))

KSH_SHELLS = ['/bin/sh', '/bin/dash', '/bin/ksh', '/bin/bash', '/usr/bin/zsh']
REBOOT_FILE = '/run/crond.reboot'

USE_LOGLEVELMAX = "@use_loglevelmax@"
USE_RUNPARTS = "@use_runparts@" == "True"
BOOT_DELAY = "@libexecdir@/systemd-cron/boot_delay"
STATEDIR = "@statedir@"

SELF = os.path.basename(sys.argv[0])
VALID_CHARS = "-_" + string.ascii_letters + string.digits

# this is dumb, but gets the job done
PART2TIMER = {
    'apt-compat': 'apt-daily',
    'dpkg': 'dpkg-db-backup',
    'plocate': 'plocate-updatedb',
    'sysstat': 'sysstat-summary',
}

CROND2TIMER = {
    'ntpsec': 'ntpsec-rotate-stats',
    'ntpsec-ntpviz': 'ntpviz-daily',
    'sysstat': 'sysstat-collect',
}

TARGET_DIR = '/tmp'

UPTIME = None

def which(exe:str, paths:Optional[str]=None) -> Optional[str]:
    if paths is None:
        paths = os.environ.get('PATH', '/usr/bin:/bin')
    for path in paths.split(os.pathsep):
        try:
            abspath = os.path.join(path, exe)
            statbuf = os.stat(abspath)
        except:
            continue
        if stat.S_IMODE(statbuf.st_mode) & 0o111:
            return abspath

    return None

HAS_SENDMAIL = bool(which('sendmail', '/usr/sbin:/usr/lib'))

def systemd_bool(string:str) -> bool:
    return string.lower() in ['yes', 'true', '1']

class Log(IntEnum):
    EMERG = 0
    ALERT = 1
    CRIT = 2
    ERR = 3
    WARNING = 4
    NOTICE = 5
    INFO = 6
    DEBUG = 7

class Job:
    '''Job definition'''
    filename:str
    basename:str
    line:str
    parts:list[str]
    environment:dict[str, str]
    shell:str
    random_delay:int
    # either period or timespec
    period:str
    timespec_minute:list[str] # 0-60
    timespec_hour:list[str] # 0-24
    timespec_dom:list[str] # 0-31
    timespec_dow:list[str] # 0-7
    timespec_month:list[str] # 0-12
    sunday_is_seven:bool
    schedule:str
    boot_delay:int
    start_hour:int
    persistent:bool
    batch:bool
    jobid:str
    unit_name:str
    user:str
    home:Optional[str]
    command:list[str]
    execstart:str
    scriptlet:str
    valid:bool
    testremoved:Optional[str]

    def __init__(self, filename:str, line:str) -> None:
        self.filename = filename
        self.basename = os.path.basename(filename)
        self.line = line
        self.parts = line.split()
        self.environment = dict()
        self.shell = '/bin/sh'
        self.boot_delay = 0
        self.start_hour = 0
        self.random_delay = 0
        self.persistent = False
        self.user = 'root'
        self.home = None
        self.command = []
        self.valid = True
        self.batch = False
        self.testremoved = None
        self.period = ''
        self.timespec_minute = []
        self.timespec_hour = []
        self.timespec_dow = []
        self.timespec_dom = []
        self.timespec_month = []
        self.sunday_is_seven = False
        self.schedule = ''

    def log(self, priority:int, message:str) -> None:
        log(priority, '%s in %s:%s' % (message, self.filename, self.line))

    def which(self, pgm) -> Optional[str]:
        return which(pgm, self.environment.get('PATH'))

    def decode_environment(self, default_persistent:bool) -> None:
        '''decode some environment variables that influence
           the behaviour of systemd-cron itself'''

        if 'SHELL' in self.environment:
            self.shell = self.environment['SHELL']

        if 'PERSISTENT' in self.environment:
            self.persistent = systemd_bool(self.environment['PERSISTENT'])
            del self.environment['PERSISTENT']
        else:
            self.persistent = default_persistent

        if 'MAILTO' in self.environment and self.environment['MAILTO']:
            if not HAS_SENDMAIL:
               self.log(Log.WARNING, 'a MTA is not installed, but MAILTO is set')

        if 'RANDOM_DELAY' in self.environment:
            try:
                self.random_delay = int(self.environment['RANDOM_DELAY'])
                del self.environment['RANDOM_DELAY']
            except ValueError:
                self.log(Log.WARNING, 'invalid RANDOM_DELAY')

        if 'START_HOURS_RANGE' in self.environment:
            try:
                self.start_hour = int(self.environment['STARTS_HOURS_RANGE'].split('-')[0])
                del self.environment['STARTS_HOURS_RANGE']
            except ValueError:
                self.log(Log.WARNING, 'invalid START_HOURS_RANGE')

        if 'DELAY' in self.environment:
            try:
                self.boot_delay = int(self.environment['DELAY'])
                del self.environment['DELAY']
            except ValueError:
                self.log(Log.WARNING, 'invalid DELAY')

        if 'BATCH' in self.environment:
            self.batch = systemd_bool(self.environment['BATCH'])
            del self.environment['BATCH']

    def parse_anacrontab(self) -> None:
        if len(self.parts) < 4:
            self.valid = False
            return

        self.period, delay, jobid = self.parts[0:3]
        self.jobid = 'anacron-' + jobid
        try:
            self.boot_delay = int(delay)
        except ValueError:
            self.log(Log.WARNING, 'invalid DELAY')
        self.command = self.parts[3:]

    def parse_crontab_auto(self) -> None:
        '''crontab --translate <something>'''
        if self.line.startswith('@'):
            self.parse_crontab_at(False)
        else:
            self.parse_crontab_timespec(False)

        if self.command:
            if len(self.command) > 1:
                maybe_user = self.command[0]
                try:
                    pwd.getpwnam(maybe_user)
                    self.user = maybe_user
                    self.command = self.command[1:]
                except:
                    self.user = os.getlogin()
            else:
                self.user = os.getlogin()

            pgm = self.which(self.command[0])
            if pgm:
                self.command[0] = pgm
            self.execstart = ' '.join(self.command)

    def parse_crontab_at(self, withuser:bool) -> None:
        '''@daily (user) do something'''
        if len(self.parts) < (2 + int(withuser)):
            self.valid = False
            return

        self.period = self.parts[0]
        if withuser:
            self.user = self.parts[1]
            self.command = self.parts[2:]
        else:
            self.user = self.basename
            self.command = self.parts[1:]
        self.jobid = self.basename + '-' + self.user

    def parse_crontab_timespec(self, withuser:bool) -> None:
        '''6 2 * * * (user) do something'''
        if len(self.parts) < (6 + int(withuser)):
            self.valid = False
            return

        minutes, hours, days, months, dows = self.parts[0:5]
        self.timespec_minute = self.parse_time_unit(minutes, MINUTES_SET)
        self.timespec_hour = self.parse_time_unit(hours, HOURS_SET)
        self.timespec_dom = self.parse_time_unit(days, DAYS_SET)
        self.timespec_dow = self.parse_time_unit(dows, DOWS_SET, dow_map)
        self.sunday_is_seven = dows.endswith('7') or dows.lower().endswith('sun')
        self.timespec_month = self.parse_time_unit(months, MONTHS_SET, month_map)

        if withuser:
            self.user = self.parts[5]
            self.command = self.parts[6:]
        else:
            self.user = self.basename
            self.command = self.parts[5:]
        self.jobid = self.basename + '-' + self.user

    def parse_time_unit(self, value:str, values, mapping=int) -> list[str]:
        result:list[str]
        if value == '*':
            return ['*']
        try:
            base = min(values)
            # day of weeks
            if isinstance(base, str):
                base = 0
            result = sorted(reduce(lambda a, i: a.union(set(i)), list(map(values.__getitem__,
            list(map(parse_period(mapping, base), value.split(','))))), set()))
        except ValueError:
            result = []
        if not len(result):
            self.log(Log.ERR, 'garbled time')
            self.valid = False
        return result

    def decode(self):
        '''decode & validate'''
        self.jobid = ''.join(c for c in self.jobid if c in VALID_CHARS)
        self.decode_command()

    def decode_command(self) -> None:
        '''perform smart substitutions for known shells'''
        if not self.home:
            # home can be set by tests to avoid
            # to read the real /etc/passwd
            try:
                self.home = pwd.getpwnam(self.user).pw_dir
            except KeyError:
                pass

        if self.home:
            if self.command[0].startswith('~/'):
                self.command[0] = self.home + self.command[0][1:]

            if 'PATH' in self.environment:
                parts = self.environment['PATH'].split(':')
                for i, part in enumerate(parts):
                    if part.startswith('~/'):
                        parts[i] = self.home + part[1:]
                self.environment['PATH'] = ':'.join(parts)

        if self.shell not in KSH_SHELLS:
            return

        pgm = self.which(self.command[0])
        if pgm and pgm != self.command[0]:
            self.command[0] = pgm

        if (len(self.command) >= 3 and
            self.command[-2] == '>' and
            self.command[-1] == '/dev/null'):
            self.command = self.command[0:-2]

        if (len(self.command) >= 2 and
            self.command[-1] == '>/dev/null'):
            self.command = self.command[0:-1]

        if (len(self.command) == 6 and
            self.command[0] == '[' and
            self.command[1] in ['-x','-f','-e'] and
            self.command[2] == self.command[5] and
            self.command[3] == ']' and
            self.command[4] == '&&' ):
                self.testremoved = self.command[2]
                self.command = self.command[5:]

        if (len(self.command) == 5 and
            self.command[0] == 'test' and
            self.command[1] in ['-x','-f','-e'] and
            self.command[2] == self.command[4] and
            self.command[3] == '&&' ):
                self.testremoved = self.command[2]
                self.command = self.command[4:]

    def is_active(self) -> bool:
        if self.testremoved and not os.path.isfile(self.testremoved):
            log(Log.NOTICE, '%s is removed, skipping job' % self.testremoved)
            return False

        if self.schedule == 'reboot' and os.path.isfile(REBOOT_FILE):
            return False

        if (len(self.command) == 6 and
            self.command[0] == '[' and
            self.command[1] in ['-d','-e'] and
            self.command[2].startswith('/run/systemd') and
            self.command[3] == ']' and
            self.command[4] == '||'):
            return False

        if (len(self.command) == 5 and
            self.command[0] == 'test' and
            self.command[1] in ['-d','-e'] and
            self.command[2].startswith('/run/systemd') and
            self.command[3] == '||'):
            return False

        return True

    def generate_schedule(self) -> None:
        if self.period:
             self.generate_schedule_from_period()
        else:
             self.generate_schedule_from_timespec()

    def generate_schedule_from_period(self) -> None:
        TIME_UNITS_SET = ['daily', 'weekly', 'monthly',
                          'quarterly', 'semi-annually', 'yearly']
        hour = self.start_hour

        self.period = self.period.lower().lstrip('@')
        self.period = {
                'boot': 'reboot',
                '1': 'daily',
                '7': 'weekly',
                '30': 'monthly',
                '31': 'monthly',
                'biannually': 'semi-annually',
                'bi-annually': 'semi-annually',
                'semiannually': 'semi-annually',
                'anually': 'yearly',
                'annually': 'yearly',
                '365': 'yearly',
        }.get(self.period, self.period)

        if self.period == 'reboot':
            self.boot_delay = max(self.boot_delay, 1)
            self.schedule = self.period
            self.persistent = False
        elif self.period == 'minutely':
            self.schedule = self.period
            self.persistent = False
        elif self.period == 'hourly' and self.boot_delay == 0:
            self.schedule = 'hourly'
        elif self.period == 'hourly':
            self.schedule = '*-*-* *:%s:0' % self.boot_delay
            self.boot_delay = 0
        elif self.period == 'midnight' and self.boot_delay == 0:
            self.schedule = 'daily'
        elif self.period == 'midnight':
            self.schedule = '*-*-* 0:%s:0' % self.boot_delay
        elif self.period in TIME_UNITS_SET and hour == 0 and self.boot_delay == 0:
            self.schedule = self.period
        elif self.period == 'daily':
            self.schedule = '*-*-* %s:%s:0' % (hour, self.boot_delay)
        elif self.period == 'weekly':
            self.schedule = 'Mon *-*-* %s:%s:0' % (hour, self.boot_delay)
        elif self.period == 'monthly':
            self.schedule = '*-*-1 %s:%s:0' % (hour, self.boot_delay)
        elif self.period == 'quarterly':
            self.schedule = '*-1,4,7,10-1 %s:%s:0' % (hour, self.boot_delay)
        elif self.period == 'semi-annually':
            self.schedule = '*-1,7-1 %s:%s:0' % (hour, self.boot_delay)
        elif self.period == 'yearly':
            self.schedule = '*-1-1 %s:%s:0' % (hour, self.boot_delay)
        else:
            try:
               period = int(self.period)
               assert type(period) is int
               if period > 31:
                    # workaround for anacrontab
                    divisor = int(round(period / 30))
                    self.schedule = '*-1/%s-1 %s:%s:0' % (divisor, hour, self.boot_delay)
               else:
                    self.schedule = '*-*-1/%s %s:%s:0' % (self.period, hour, self.boot_delay)
            except ValueError:
               self.log(Log.ERR, 'unknown schedule')
               self.schedule = self.period

    def generate_schedule_from_timespec(self) -> None:
        if self.timespec_dow == ['*']:
            dows = ''
        else:
            dows_sorted = []
            for day in DOWS_SET[int(self.sunday_is_seven):]:
                if day in self.timespec_dow and not day in dows_sorted:
                    dows_sorted.append(day)
            dows = ','.join(dows_sorted) + ' '

        if '0' in self.timespec_month:
            self.timespec_month.remove('0')
        if '0' in self.timespec_dom:
            self.timespec_dom.remove('0')

        if (not len(self.timespec_month) or
           not len(self.timespec_dom) or
           not len(self.timespec_hour) or
           not len(self.timespec_minute)):
            self.valid = False
            self.log(Log.ERR, 'unknown schedule')
            return None

        self.schedule = '%s*-%s-%s %s:%s:00' % (
                      dows,
                      ','.join(map(str, self.timespec_month)),
                      ','.join(map(str, self.timespec_dom)),
                      ','.join(map(str, self.timespec_hour)),
                      ','.join(map(str, self.timespec_minute))
                   )

    def generate_scriptlet(self) -> Optional[str]:
        '''...only if needed'''
        assert self.unit_name
        if len(self.command) == 1:
            if os.path.isfile(self.command[0]):
                self.execstart = self.command[0]
                return None

        self.scriptlet = os.path.join(TARGET_DIR, '%s.sh' % self.unit_name)
        self.execstart = self.shell + ' ' + self.scriptlet
        return ' '.join(self.command)

    def generate_service(self) -> str:
        lines = list()
        lines.append('[Unit]')
        lines.append('Description=[Cron] "%s"' % self.line.replace('%', '%%'))
        lines.append('Documentation=man:systemd-crontab-generator(8)')
        if self.filename != '-':
            lines.append('SourcePath=%s' % self.filename)
        if 'MAILTO' in self.environment and not self.environment['MAILTO']:
            pass # mails explicitely disabled
        elif not HAS_SENDMAIL:
            pass # mails automaticaly disabled
        else:
            lines.append('OnFailure=cron-failure@%i.service')
        if self.user != 'root' or STATEDIR in self.filename:
            lines.append('Requires=systemd-user-sessions.service')
            if self.home:
                lines.append('RequiresMountsFor=%s' % self.home)
        lines.append('')

        lines.append('[Service]')
        lines.append('Type=oneshot')
        lines.append('IgnoreSIGPIPE=false')
        lines.append('KillMode=process')
        if USE_LOGLEVELMAX != 'no':
            lines.append('LogLevelMax=%s' % USE_LOGLEVELMAX)
        if self.schedule and self.boot_delay:
            if UPTIME is None or self.boot_delay > UPTIME:
                lines.append('ExecStartPre=-%s %s' % (BOOT_DELAY, self.boot_delay))
        lines.append('ExecStart=%s' % self.execstart)
        if self.environment:
            lines.append('Environment=%s' % environment_string(self.environment))
        lines.append('User=%s' % self.user)
        if self.batch:
            lines.append('CPUSchedulingPolicy=idle')
            lines.append('IOSchedulingClass=idle')

        return '\n'.join(lines)

    def generate_timer(self) -> str:
        lines = list()
        lines.append('[Unit]')
        lines.append('Description=[Timer] "%s"' % self.line.replace('%', '%%'))
        lines.append('Documentation=man:systemd-crontab-generator(8)')
        lines.append('PartOf=cron.target')
        if self.filename != '-':
            lines.append('SourcePath=%s' % self.filename)
        if self.testremoved:
            lines.append('ConditionFileIsExecutable=%s' % self.testremoved)
        lines.append('')

        lines.append('[Timer]')
        if self.schedule == 'reboot':
            lines.append('OnBootSec=%sm' % self.boot_delay)
        else:
            lines.append('OnCalendar=%s' % self.schedule)
        if self.random_delay > 1:
            lines.append('RandomizedDelaySec=%sm' % self.random_delay)
        if self.persistent:
            lines.append('Persistent=true')

        return '\n'.join(lines)

    def generate_unit_name(self, seq) -> None:
        assert self.jobid
        if not self.persistent:
            unit_id = next(seq)
        else:
            unit_id = hashlib.md5()
            unit_id.update(bytes('\0'.join([self.schedule] + self.command), 'utf-8'))
            unit_id = unit_id.hexdigest()
        self.unit_name = "cron-%s-%s" % (self.jobid, unit_id)

    def output(self) -> None:
        '''write the result in TARGET_DIR'''
        assert self.unit_name

        code = self.generate_scriptlet() # as a side-effect also changes self.execstart
        if code:
            with open(self.scriptlet, 'w', encoding='utf8') as f:
                f.write(code + '\n')

        timer = os.path.join(TARGET_DIR, '%s.timer' % self.unit_name)
        with open(timer, 'w', encoding='utf8') as f:
            f.write(self.generate_timer() + '\n')

        try:
            os.symlink(timer, os.path.join(TIMERS_DIR, '%s.timer' % self.unit_name))
        except OSError as e:
            if e.errno != errno.EEXIST:
               raise

        service = os.path.join(TARGET_DIR, '%s.service' % self.unit_name)
        with open(service, 'w', encoding='utf8') as f:
            f.write(self.generate_service() + '\n')


def files(dirname:str) -> list[str]:
    try:
        return list(filter(os.path.isfile, [os.path.join(dirname, f) for f in os.listdir(dirname)]))
    except OSError:
        return []

def environment_string(env:dict[str, str]) -> str:
    line = []
    for k, v in sorted(env.items()):
        if ' ' in v:
            line.append('"%s=%s"' % (k, v))
        else:
            line.append('%s=%s' % (k, v))
    return ' '.join(line)

def parse_crontab(filename:str,
                  withuser:bool=True,
                  monotonic:bool=False) -> Iterator[Job]:
    '''parser shared with /usr/bin/crontab'''

    environment:dict[str,str] = dict()
    with open(filename, 'rb') as f:
        for rawline in f.readlines():
            rawline = rawline.strip()
            if not rawline or rawline.startswith(b'#'):
                continue

            try:
                line = rawline.decode('utf8')
            except UnicodeDecodeError:
                # let's hope it's in a trailing comment
                try:
                    line = rawline.split(b'#')[0].decode('utf8')
                except UnicodeDecodeError:
                    line = rawline.decode('ascii', 'replace')

            while '  ' in line:
                line = line.replace('  ', ' ')

            envvar = ENVVAR_RE.match(line)
            if envvar:
                key = envvar.group(1)
                value = envvar.group(2)
                value = value.strip("'").strip('"').strip(' ')
                if key == 'PERSISTENT' and value == 'auto':
                    environmanet.pop(key, None)
                else:
                    environment[key] = value
                continue

            j = Job(filename, line)
            j.environment = copy.deepcopy(environment)
            if monotonic:
                j.decode_environment(default_persistent=True)
                j.parse_anacrontab()
            elif line.startswith('@'):
                j.decode_environment(default_persistent=True)
                j.parse_crontab_at(withuser)
            else:
                j.decode_environment(default_persistent=False)
                j.parse_crontab_timespec(withuser)
            j.decode()
            j.generate_schedule()
            yield j


def month_map(month:str) -> int:
    try:
        return int(month)
    except ValueError:
        return ['jan', 'feb', 'mar', 'apr',
                'may', 'jun', 'jul', 'aug',
                'sep', 'oct', 'nov', 'dec'].index(month.lower()[0:3]) + 1

def dow_map(dow:str) -> int:
    try:
        return ['sun', 'mon', 'tue', 'wed', 'thu', 'fri', 'sat'].index(dow[0:3].lower())
    except ValueError:
        return int(dow) #% 7

def parse_period(mapping=int, base=0):
    def parser(value:str):
        try:
            range, step = value.split('/')
        except ValueError:
            range = value
            step = '1'

        if range == '*':
            return slice(None, None, int(step))

        try:
            start, end = range.split('-')
        except ValueError:
            start = end = range

        return slice(mapping(start) - 1 + int(not(bool(base))), mapping(end) + int(not(bool(base))), int(step))

    return parser

seqs:dict[str, int] = {}
def count():
    n = 0
    while True:
        yield n
        n += 1

def generate_timer_unit(job:Job):
    if job.valid and job.is_active():
        job.generate_unit_name(seqs.setdefault(job.jobid, count()))
        job.output()

def log(level:int, message:str) -> None:
    if len(sys.argv) == 4:
        with open('/dev/kmsg', 'w', encoding='utf8') as kmsg:
            kmsg.write('<%s>%s[%s]: %s\n' % (level, SELF, os.getpid(), message))
    else:
        sys.stderr.write('%s: %s\n' % (SELF, message))

def workaround_var_not_mounted():
    '''schedule rerun of generators after /var is mounted'''
    with open('%s/cron-after-var.service' % TARGET_DIR, 'w') as f:
        f.write('[Unit]\n')
        f.write('Description=Rerun systemd-crontab-generator because /var is a separate mount\n')
        f.write('Documentation=man:systemd.cron(7)\n')
        f.write('After=cron.target\n')
        f.write('ConditionDirectoryNotEmpty=%s\n' % STATEDIR)
        f.write('\n[Service]\n')
        f.write('Type=oneshot\n')
        f.write('ExecStart=/bin/sh -c "systemctl daemon-reload ; systemctl try-restart cron.target"\n')

    MULTIUSER_DIR = os.path.join(TARGET_DIR, 'multi-user.target.wants')

    try:
       os.makedirs(MULTIUSER_DIR)
    except OSError as e:
       if e.errno != errno.EEXIST:
           raise

    try:
        os.symlink('%s/cron-after-var.service' % TARGET_DIR, '%s/cron-after-var.service' % MULTIUSER_DIR)
    except OSError as e:
        if e.errno != errno.EEXIST:
            raise

def is_masked(path:str, name:str, distro_mapping:dict[str,str]) -> bool:
    '''check if distribution also provide a native .timer'''
    fullname = os.path.join(path, name)
    for unit_file in ('/lib/systemd/system/%s.timer' % name,
                      '/etc/systemd/system/%s.timer' % name,
                      '/run/systemd/system/%s.timer' % name):
        if os.path.exists(unit_file):
            if os.path.realpath(unit_file) == '/dev/null':
                # TODO: check 0-byte file
                reason = 'it is masked'
            else:
                reason = 'native timer is present'
            log(Log.NOTICE, 'ignoring %s because %s' % (fullname, reason))
            return True

    name_distro = '%s.timer' % distro_mapping.get(name, name)
    if os.path.exists('/lib/systemd/system/%s' % name_distro):
        log(Log.NOTICE, 'ignoring %s because there is %s' % (fullname, name_distro))
        return True

    return False

def is_backup(name:str) -> bool:
    if '.dpkg-' in name:
        return True
    if '~' in name:
        return True
    if name.startswith('.'):
        return True
    return False

def main() -> None:
    try:
        os.makedirs(TIMERS_DIR)
    except OSError as e:
        if e.errno != errno.EEXIST:
            raise

    fallback_mailto = None

    if os.path.isfile('/etc/crontab'):
        for job in parse_crontab('/etc/crontab', withuser=True):
            fallback_mailto = job.environment.get('MAILTO')
            if not job.valid:
                 log(Log.ERR, 'truncated line in /etc/crontab: %s' % job.line)
                 continue
            # legacy boilerplate
            if '/etc/cron.hourly'  in job.line: continue
            if '/etc/cron.daily'   in job.line: continue
            if '/etc/cron.weekly'  in job.line: continue
            if '/etc/cron.monthly' in job.line: continue
            generate_timer_unit(job)

    CRONTAB_FILES = files('/etc/cron.d')
    for filename in CRONTAB_FILES:
        basename = os.path.basename(filename)
        if is_masked('/etc/cron.d', basename, CROND2TIMER):
            continue
        if is_backup(basename):
            log(Log.DEBUG, 'ignoring %s' % filename)
            continue
        for job in parse_crontab(filename, withuser=True):
            if not job.valid:
                log(Log.ERR, 'truncated line in %s: %s' % (filename, job.line))
                continue
            if fallback_mailto and 'MAILTO' not in job.environment:
                job.environment['MAILTO'] = fallback_mailto
            generate_timer_unit(job)

    if not USE_RUNPARTS:
        i = 0
        for period in ['hourly', 'daily', 'weekly', 'monthly', 'yearly']:
            i = i + 1
            directory = '/etc/cron.' + period
            if not os.path.isdir(directory):
                continue
            CRONTAB_FILES = files('/etc/cron.' + period)
            for filename in CRONTAB_FILES:
                basename = os.path.basename(filename)
                if is_masked(directory, basename, PART2TIMER):
                    continue
                if is_backup(basename) or basename == '0anacron':
                    log(Log.DEBUG, 'ignoring %s' % filename)
                    continue

                job = Job(filename, filename)
                job.persistent = True
                job.period = period
                job.boot_delay = i * 5
                job.command = [filename]
                job.jobid = period + '-' + basename
                job.decode() # ensure clean jobid
                job.generate_schedule()
                if fallback_mailto and 'MAILTO' not in job.environment:
                    job.environment['MAILTO'] = fallback_mailto
                job.unit_name = 'cron-' + job.jobid
                job.output()

    if os.path.isfile('/etc/anacrontab'):
        for job in parse_crontab('/etc/anacrontab', monotonic=True):
            if not job.valid:
                 log(Log.ERR, 'truncated line in /etc/anacrontab: %s' % job.line)
                 continue
            generate_timer_unit(job)

    if os.path.isdir(STATEDIR):
        # /var is avaible
        USERCRONTAB_FILES = files(STATEDIR)
        for filename in USERCRONTAB_FILES:
            basename = os.path.basename(filename)
            if '.' in basename:
                continue
            for job in parse_crontab(filename, withuser=False):
                generate_timer_unit(job)
        try:
            open(REBOOT_FILE,'a').close()
        except:
            pass
    else:
        workaround_var_not_mounted()


if __name__ == '__main__':
    if len(sys.argv) == 1 or (os.path.exists(sys.argv[1])
                      and not os.path.isdir(sys.argv[1])):
        sys.exit("Usage: %s <destination_folder>" % sys.argv[0])

    TARGET_DIR = sys.argv[1]
    TIMERS_DIR = os.path.join(TARGET_DIR, 'cron.target.wants')

    run_by_systemd = len(sys.argv) == 4
    if run_by_systemd:
        with open('/proc/uptime', 'r') as f:
            UPTIME = int(float(f.readline().split()[0])) / 60

    try:
        main()
    except Exception as e:
        if run_by_systemd:
            with open('/dev/kmsg', 'w') as fd:
                fd.write('<%s> %s[%s]: global exception: %s\n' % (Log.CRIT, SELF, os.getpid(), e))
            exit(1)
        else:
            raise
