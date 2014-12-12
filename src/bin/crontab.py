#!/usr/bin/python3

import tempfile
import sys
import os
import stat
import argparse
import getpass
import pwd
import subprocess
from subprocess import Popen, PIPE
import importlib.machinery

EDITOR = (os.environ.get('EDITOR') or
          os.environ.get('VISUAL','/usr/bin/vim'))
CRONTAB_DIR = '@statedir@'

SETUID_HELPER = '@libdir@/@package@/crontab_setuid'

HAS_SETUID =     os.geteuid() != 0 \
             and os.path.isfile(SETUID_HELPER) \
             and os.stat(SETUID_HELPER).st_uid == 0 \
             and os.stat(SETUID_HELPER).st_mode & stat.S_ISUID \
             and os.stat(SETUID_HELPER).st_mode & stat.S_IXUSR

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

args_parser.add_argument('-i', '--ask', dest='ask', action='store_true', default=False,
        help='''This option modifies the -r option to prompt the user for a
 'y/Y' response before actually removing the crontab.''')

#args_parser.add_argument('-s', '--secure', dest='secure', action='store_true', default=False,
        #help='''It will append the current SELinux security context string as an
 #MLS_LEVEL setting to the crontab file before editing / replacement occurs
 #- see the documentation of MLS_LEVEL in crontab(5).''')

def confirm(message):
    while True:
        answer = input(message).lower()
        if answer not in 'yn':
            print('Please reply "y" or "n"')
            continue

        return answer == 'y'

def check(cron_file):
    good = True
    for job in parser.parse_crontab(cron_file, withuser=False):
        if 'c' not in job:
            good = False
            sys.stderr.write('%s: truncated line in %s: %s\n' % (SELF, job['f'], job['l']))
        elif 'p' in job:
            if job['p'] not in ['reboot', 'minutely', 'hourly', 'daily', 'midnight', 'weekly',
                                'monthly', 'quarterly',
                                'semi-annually', 'semiannually', 'bi-annually', 'biannually',
                                'annually', 'yearly']:
                good = False
                sys.stderr.write("%s: unknown schedule in %s: %s\n" % (SELF, job['f'], job['l']))
        elif 0 in job['M'] or 0 in job['d']:
                good = False
                sys.stderr.write("%s: month and day can't be 0 in %s: %s\n" % (SELF, job['f'], job['l']))
        else:
            if not len(job['M']) or not len(job['d']) or not len(job['h']) or not len(job['m']):
                good = False
    return good

def list(cron_file, args):
    try:
        with open(cron_file, 'r') as f:
            sys.stdout.write(f.read())
        check(cron_file)
    except IOError as e:
        if e.errno == os.errno.ENOENT:
            sys.stderr.write('no crontab for %s\n' % args.user)
            exit(1)
        elif args.user != getpass.getuser():
            sys.stderr.write("you can not display %s's crontab\n" % args.user)
            exit(1)
        elif HAS_SETUID:
            try:
                sys.stdout.write(subprocess.check_output([SETUID_HELPER,'r'], universal_newlines=True))
            except subprocess.CalledProcessError as f:
                if f.returncode == os.errno.ENOENT:
                    sys.stderr.write('no crontab for %s\n' % args.user)
                    exit(1)
                else:
                    raise
            pass
        else:
            raise

def remove(cron_file, args):
    if not args.ask or confirm('Are you sure you want to delete %s (y/n)? ' % cron_file):
        try:
            os.unlink(cron_file)
        except OSError as e:
            if e.errno == os.errno.ENOENT:
                sys.stderr.write("no crontab for %s\n" % args.user)
                pass
            elif args.user != getpass.getuser():
                sys.stderr.write("you can not delete %s's crontab\n" % args.user)
                exit(1)
            elif HAS_SETUID:
                subprocess.check_output([SETUID_HELPER,'d'], universal_newlines=True)
                pass
            elif e.errno == os.errno.EACCES:
                with open(cron_file, 'w') as out:
                    out.write('')
                sys.stderr.write("couldn't remove %s , wiped it instead\n" % cron_file)
                pass
            else:
                raise

def edit(cron_file, args):
    tmp = tempfile.NamedTemporaryFile(mode='w+', encoding='UTF-8', delete=False, prefix='crontab_')

    try:
        with open(cron_file, 'r') as inp:
            tmp.file.write(inp.read())
    except IOError as e:
        if e.errno == os.errno.ENOENT:
            tmp.file.write('# min hour dom month dow command')
            pass
        elif args.user != getpass.getuser():
            sys.stderr.write("you can not edit %s's crontab\n" % args.user)
            tmp.close()
            os.unlink(tmp.name)
            exit(1)
        elif HAS_SETUID:
            try:
                tmp.file.write(subprocess.check_output([SETUID_HELPER,'r'], universal_newlines=True))
            except subprocess.CalledProcessError as f:
                if f.returncode == os.errno.ENOENT:
                    tmp.file.write('# min hour dom month dow command')
                    pass
                else:
                    tmp.close()
                    os.unlink(tmp.name)
                    raise
            pass
        else:
            tmp.close()
            os.unlink(tmp.name)
            raise

    tmp.file.flush()

    if os.system("'%s' '%s'" % (EDITOR, tmp.name)) != 0:
        tmp.close()
        sys.stderr.write('edit aborted, your edit is kept here:%s\n' % tmp.name)
        exit(1)

    if not check(tmp.name):
        tmp.close()
        sys.stderr.write("not replacing crontab, your edit is kept here:%s\n" % tmp.name)
        exit(1)

    tmp.file.seek(0)

    try:
        new = tempfile.NamedTemporaryFile(mode='w+', encoding='UTF-8', dir=CRONTAB_DIR,
                                          prefix=args.user + '.', delete=False)
        new.write(tmp.file.read())
        new.close()
        os.rename(new.name, cron_file)
        tmp.close()
        os.unlink(tmp.name)
        try:
            os.chown(cron_file, pwd.getpwnam(args.user).pw_uid, 0)
            os.chmod(cron_file, stat.S_IRUSR | stat.S_IWUSR)
        except PermissionError:
            pass
    except (IOError, PermissionError) as e:
        if e.errno == os.errno.ENOSPC:
            sys.stderr.write("no space left on %s, your edit is kept here:%s\n" % (CRONTAB_DIR, tmp.name))
            os.unlink(new.name)
            tmp.close()
            exit(1)
        elif args.user != getpass.getuser():
            sys.stderr.write("you can not edit %s's crontab, your edit is kept here:%s\n" % (args.user, tmp.name))
            tmp.close()
            exit(1)
        elif HAS_SETUID:
            p = Popen([SETUID_HELPER,'w'], stdin=PIPE)
            p.communicate(bytes(tmp.file.read(), 'UTF-8'))
            if p.returncode: sys.stderr.write("your edit is kept here:%s\n" % tmp.name)
            exit(p.returncode)
        else:
            sys.stderr.write("unexpected error, your edit is kept here:%s\n" % tmp.name)
            tmp.close()
            raise


def replace(cron_file, args):
    if args.file == '-':
        crontab = sys.stdin.read()
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
            if e.errno == os.errno.ENOENT:
                sys.stderr.write("file %s doesn't exists\n" % infile)
                exit(1)
            elif e.errno == os.errno.EACCES:
                sys.stderr.write("you can't read file %s\n" % infile)
                exit(1)
            else:
                raise

    if not check(infile):
        sys.stderr.write("not replacing crontab\n")
        exit(1)

    try:
        new = tempfile.NamedTemporaryFile(mode='w+', encoding='UTF-8', dir=CRONTAB_DIR,
                                          prefix=args.user + '.', delete=False)
        new.write(crontab)
        new.close()
        os.rename(new.name, cron_file)
        try:
            os.chown(cron_file, pwd.getpwnam(args.user).pw_uid, 0)
            os.chmod(cron_file, stat.S_IRUSR | stat.S_IWUSR)
        except PermissionError:
            pass
    except (IOError, PermissionError) as e:
        if args.user != getpass.getuser():
            sys.stderr.write("you can not replace %s's crontab\n" % args.user)
            exit(1)
        elif e.errno == os.errno.ENOSPC:
            sys.stderr.write("no space left on %s\n" % CRONTAB_DIR)
            os.unlink(new.name)
            exit(1)
        elif HAS_SETUID:
            p = Popen([SETUID_HELPER,'w'], stdin=PIPE)
            p.communicate(bytes(crontab, 'UTF-8'))
            exit(p.returncode)
        else:
            raise


if __name__ == '__main__':
    SELF = os.path.basename(sys.argv[0])
    try:
        if not os.path.exists(CRONTAB_DIR):
            os.makedirs(CRONTAB_DIR)
    except:
        sys.stderr.write("%s doesn't exists!\n" % CRONTAB_DIR)
        exit(1)

    args = args_parser.parse_args()

    # deluser in adduser package expect this behaviour
    if args.file != '-' and args.action in ['list', 'edit', 'remove']:
        args.user = args.file

    cron_file = os.path.join(CRONTAB_DIR, args.user)

    try:
        pwd.getpwnam(args.user)
    except KeyError:
        sys.stderr.write("user '%s' unknown\n" % args.user)
        exit(1)

    try:
        open(REBOOT_FILE,'a').close()
    except:
        pass

    action = {
            'list': list,
            'edit': edit,
            'remove': remove,
            }.get(args.action, replace)

    loader = importlib.machinery.SourceFileLoader('name', '@libdir@/systemd/system-generators/systemd-crontab-generator')
    parser = loader.load_module()

    action(cron_file, args)
