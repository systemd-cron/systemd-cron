#!/usr/bin/python3

import argparse
import errno
import getpass
import glob
import importlib.machinery
import os
import pwd
import stat
import sys
import subprocess
import tempfile
from subprocess import Popen, PIPE
from typing import Optional

for pgm in ('/usr/bin/editor', '/usr/bin/vim', '/usr/bin/nano', '/usr/bin/mcedit'):
    if os.path.isfile(pgm) and os.access(pgm, os.X_OK):
        editor = pgm
        break
else:
    editor = 'false'

EDITOR = (os.environ.get('EDITOR') or
          os.environ.get('VISUAL') or
          editor)

CRONTAB_DIR = '@statedir@'

SETGID_HELPER = '@libdir@/@package@/crontab_setgid'

HAS_SETGID =     os.geteuid() != 0 \
             and os.path.isfile(SETGID_HELPER) \
             and os.stat(SETGID_HELPER).st_uid == 0 \
             and os.stat(SETGID_HELPER).st_gid != 0 \
             and os.stat(SETGID_HELPER).st_mode & stat.S_ISGID \
             and os.stat(SETGID_HELPER).st_mode & stat.S_IXGRP

REBOOT_FILE = '/run/crond.reboot'

args_parser = argparse.ArgumentParser(description='maintain crontab files for individual users')

group = args_parser.add_mutually_exclusive_group()

args_parser.add_argument('-u', '--user', type=str, dest='user', default=getpass.getuser(),
        help='''It specifies the name of the user whose crontab is to be
 tweaked. If this option is not given, crontab examines "your" crontab, i.e., the crontab of the person
 executing the command. Note that su(8) can confuse crontab and that if you are running inside of su(8) you
 should always use the -u option for safety's sake. The first form of this command is used to install a new
 crontab from some named file or standard input if the pseudo-filename "-" is given.''')

args_parser.add_argument('file', type=str, default='-', nargs='?')

group.add_argument('-l', '--list', dest='action', action='store_const', const='list',
        help='''The current crontab will be displayed on standard output.''')

group.add_argument('-r', '--remove', dest='action', action='store_const', const='remove',
        help='''The current crontab will be removed.''')

group.add_argument('-e', '--edit', dest='action', action='store_const', const='edit',
        help='''This option is used to edit the current crontab using the editor
 specified by the VISUAL or EDITOR environment variables. After
 you exit from the editor, the modified crontab will be installed
 automatically.''')

group.add_argument('-s', '--show', dest='action', action='store_const', const='show',
        help='''Show all user who have a crontab.''')

args_parser.add_argument('-i', '--ask', dest='ask', action='store_true', default=False,
        help='''This option modifies the -r option to prompt the user for a
 'y/Y' response before actually removing the crontab.''')

#args_parser.add_argument('-s', '--secure', dest='secure', action='store_true', default=False,
        #help='''It will append the current SELinux security context string as an
 #MLS_LEVEL setting to the crontab file before editing / replacement occurs
 #- see the documentation of MLS_LEVEL in crontab(5).''')

def confirm(message:str) -> bool:
    while True:
        answer = input(message).lower()
        if answer not in 'yn':
            print('Please reply "y" or "n"')
            continue

        return answer == 'y'

def check(cron_file:str) -> bool:
    good = True
    for job in parser.parse_crontab(cron_file, withuser=False):
        if not job.valid:
            good = False
            sys.stderr.write('%s: truncated line in %s: %s\n' % (SELF, cron_file, job.line))
        elif type(job.period) is str:
            if job.period not in ['reboot', 'minutely', 'hourly', 'daily', 'midnight', 'weekly',
                                'monthly', 'quarterly',
                                'semi-annually', 'semiannually', 'bi-annually', 'biannually',
                                'annually', 'yearly']:
                good = False
                sys.stderr.write("%s: unknown schedule in %s: %s\n" % (SELF, cron_file, job.line))
        elif 0 in job.period['M'] or 0 in job.period['d']:
                good = False
                sys.stderr.write("%s: month and day can't be 0 in %s: %s\n" % (SELF, cron_file, job.line))
        else:
            if (not len(job.period['M'])
                or not len(job.period['d'])
                or not len(job.period['h'])
                or not len(job.period['m'])):
                good = False
    return good

def try_chmod(cron_file:str, user:str):
    if CRON_GROUP:
        try:
            os.chown(cron_file, pwd.getpwnam(user).pw_uid, CRON_GROUP)
            os.chmod(cron_file, stat.S_IRUSR | stat.S_IWUSR)
        except (PermissionError, KeyError):
            pass

def list(cron_file:str, args) -> None:
    try:
        with open(cron_file, 'r') as f:
            sys.stdout.write(f.read())
        check(cron_file)
        try_chmod(cron_file, args.user)
    except (IOError, PermissionError) as e:
        if e.errno == errno.ENOENT:
            sys.exit('no crontab for %s' % args.user)
        elif args.user != getpass.getuser():
            sys.exit("you can not display %s's crontab" % args.user)
        elif HAS_SETGID:
            returncode = subprocess.call([SETGID_HELPER,'r'])
            if returncode == errno.ENOENT:
                sys.exit('no crontab for %s' % args.user)
            elif returncode:
                # helper will send error to stderr
                sys.exit('failed to read %s' % cron_file)
        else:
            raise

def remove(cron_file:str, args):
    if not args.ask or confirm('Are you sure you want to delete %s (y/n)? ' % cron_file):
        try:
            os.unlink(cron_file)
        except OSError as e:
            if e.errno == errno.ENOENT:
                sys.stderr.write("no crontab for %s\n" % args.user)
            elif args.user != getpass.getuser():
                sys.exit("you can not delete %s's crontab" % args.user)
            elif e.errno == errno.EROFS:
                if os.path.isfile(cron_file):
                    sys.exit("%s is on a read-only filesystem" % cron_file)
                else:
                    sys.stderr.write("no crontab for %s\n" % args.user)
            elif HAS_SETGID:
                subprocess.check_output([SETGID_HELPER,'d'], universal_newlines=True)
            elif e.errno == errno.EACCES:
                open(cron_file, 'w').close()
                sys.stderr.write("couldn't remove %s , wiped it instead\n" % cron_file)
            else:
                raise

def edit(cron_file:str, args):
    tmp = tempfile.NamedTemporaryFile(mode='w+', encoding='UTF-8', delete=False, prefix='crontab_')

    try:
        with open(cron_file, 'r') as inp:
            tmp.file.write(inp.read())
    except IOError as e:
        if e.errno == errno.ENOENT:
            tmp.file.write('# min hour dom month dow command')
        elif args.user != getpass.getuser():
            sys.stderr.write("you can not edit %s's crontab\n" % args.user)
            tmp.close()
            os.unlink(tmp.name)
            exit(1)
        elif HAS_SETGID:
            try:
                tmp.file.write(subprocess.check_output([SETGID_HELPER,'r'], universal_newlines=True))
            except subprocess.CalledProcessError as f:
                if f.returncode == errno.ENOENT:
                    tmp.file.write('# min hour dom month dow command')
                else:
                    # helper will send error to stderr
                    sys.stderr.write('failed to read %s\n' % cron_file)
                    tmp.close()
                    os.unlink(tmp.name)
                    exit(f.returncode)
        else:
            tmp.close()
            os.unlink(tmp.name)
            raise

    tmp.close()

    if subprocess.call(EDITOR.split(" ") + [tmp.name]) != 0:
        sys.exit('edit aborted, your edit is kept here:%s' % tmp.name)

    if not check(tmp.name):
        sys.exit("not replacing crontab, your edit is kept here:%s" % tmp.name)

    with open(tmp.name,"r") as edited:
        newtab = edited.read()

    try:
        new = tempfile.NamedTemporaryFile(mode='w+', encoding='UTF-8', dir=CRONTAB_DIR,
                                          prefix=args.user + '.', delete=False)
        new.write(newtab)
        new.close()
        os.rename(new.name, cron_file)
        os.unlink(tmp.name)
        try_chmod(cron_file, args.user)
    except (IOError, PermissionError) as e:
        if e.errno == errno.ENOSPC:
            os.unlink(new.name)
            sys.exit("no space left on %s, your edit is kept here:%s" % (CRONTAB_DIR, tmp.name))
        elif args.user != getpass.getuser():
            sys.exit("you can not edit %s's crontab, your edit is kept here:%s" % (args.user, tmp.name))
        elif e.errno == errno.EROFS:
            sys.exit("%s is on a read-only filesystem, your edit is kept here:%s" % (CRONTAB_DIR, tmp.name))
        elif HAS_SETGID:
            p = Popen([SETGID_HELPER,'w'], stdin=PIPE)
            p.communicate(bytes(newtab, 'UTF-8'))
            if p.returncode:
                sys.exit("your edit is kept here:%s" % tmp.name)
            else:
                os.unlink(tmp.name)
        else:
            sys.stderr.write("unexpected error, your edit is kept here:%s\n" % tmp.name)
            raise

def show(cron_file:str, args):
    if os.geteuid() != 0:
        sys.exit("must be privileged to use -s")

    for cron_file in glob.glob(os.path.join(CRONTAB_DIR, '*')):
        user = os.path.basename(cron_file)
        try:
            pwd.getpwnam(user)
            print(user)
        except KeyError:
            sys.stderr.write("WARNING: crontab found with no matching user: %s\n" % user)

def replace(cron_file:str, args):
    if args.file == '-':
        try:
            crontab = sys.stdin.read()
        except KeyboardInterrupt:
            sys.exit()
        tmp = tempfile.NamedTemporaryFile(mode='w+', encoding='UTF-8')
        tmp.write(crontab)
        tmp.file.flush()
        tmp.file.seek(0)
        infile = tmp.name
    else:
        infile = args.file
        try:
            with open(infile, 'r') as inp:
                crontab = inp.read()
        except IOError as e:
            if e.errno == errno.ENOENT:
                sys.exit("file %s doesn't exists" % infile)
            elif e.errno == errno.EACCES:
                sys.exit("you can't read file %s" % infile)
            else:
                raise

    if not check(infile):
        sys.exit("not replacing crontab\n")

    try:
        new = tempfile.NamedTemporaryFile(mode='w+', encoding='UTF-8', dir=CRONTAB_DIR,
                                          prefix=args.user + '.', delete=False)
        new.write(crontab)
        new.close()
        os.rename(new.name, cron_file)
        try_chmod(cron_file, args.user)
    except (IOError, PermissionError) as e:
        if args.user != getpass.getuser():
            sys.exit("you can not replace %s's crontab" % args.user)
        elif e.errno == errno.ENOSPC:
            sys.stderr.write("no space left on %s\n" % CRONTAB_DIR)
            os.unlink(new.name)
            exit(1)
        elif e.errno == errno.EROFS:
            sys.exit("%s is on a read-only filesystem" % CRONTAB_DIR)
        elif HAS_SETGID:
            p = Popen([SETGID_HELPER,'w'], stdin=PIPE)
            p.communicate(bytes(crontab, 'UTF-8'))
            exit(p.returncode)
        else:
            raise


if __name__ == '__main__':
    SELF = os.path.basename(sys.argv[0])

    # try to fixup CRONTAB_DIR if it has not been handled in package script
    try:
        if not os.path.exists(CRONTAB_DIR):
            os.makedirs(CRONTAB_DIR)
    except:
        sys.exit("%s doesn't exists!" % CRONTAB_DIR)

    CRON_GROUP:Optional[int]
    try:
        CRON_GROUP = os.stat(SETGID_HELPER).st_gid
    except:
        CRON_GROUP = None

    if CRON_GROUP:
        try:
            os.chown(CRONTAB_DIR, 0, CRON_GROUP)
            os.chmod(CRONTAB_DIR, stat.S_ISVTX | stat.S_IRUSR | stat.S_IWUSR | stat.S_IXUSR | stat.S_IWGRP | stat.S_IXGRP)
        except:
            pass

    args = args_parser.parse_args()

    # deluser in adduser package expect this behaviour
    if args.file != '-' and args.action in ['list', 'edit', 'remove']:
        args.user = args.file

    cron_file = os.path.join(CRONTAB_DIR, args.user)

    try:
        pwd.getpwnam(args.user)
    except KeyError:
        sys.exit("user '%s' unknown" % args.user)

    try:
        open(REBOOT_FILE,'a').close()
    except:
        pass

    action = {
            'list': list,
            'edit': edit,
            'remove': remove,
            'show': show,
            }.get(args.action, replace)

    loader = importlib.machinery.SourceFileLoader('name', '@generatordir@/systemd-crontab-generator')
    parser = loader.load_module()

    action(cron_file, args)
