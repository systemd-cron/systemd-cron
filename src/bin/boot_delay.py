#!/usr/bin/python3
import sys
import time

try:
    delay = int(sys.argv[1]) * 60
except:
    sys.exit("Usage: %s <minutes>" % sys.argv[0])

f = open('/proc/uptime', 'r')
uptime = int(float(f.readline().split()[0]))

if delay > uptime:
    try:
        time.sleep(delay - uptime)
    except KeyboardInterrupt:
        pass
