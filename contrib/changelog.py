#!/usr/bin/python3
import re
import time

# lynx -dump https://github.com/systemd-cron/systemd-cron/releases

with open('releases', 'r', encoding='utf8') as f:
    while f.readline():
        # get release
        while True:
            line = f.readline()
            # should use a regexp
            if line.startswith('[') and ']v' in line:
                break
        version = line.split(']')[1].strip()

        # get date
        f.readline()
        block = f.readline().rstrip() + f.readline()
        date = block[block.find('released this')+13:-1][1:13].strip()
        try:
            date = time.strftime('%Y-%m-%d',time.strptime(date,"%b %d, %Y"))
        except ValueError:
            pass

        print("%s : %s\n" % (version, date))       

        # get details
        first = True
        while True:
            line = f.readline().rstrip()
            line = re.sub('\[\d*\]', '', line)
            if 'Source code (zip)' in line:
               break
            if line.strip() == 'systemd-cron':
               break
            if line or not first:
               if line and not line.startswith(' '):
                    print ('      ', end='')
               print(line)
               first = False
            last = line
        if last:
            print()

        if version == 'v0.1.0':
            break
