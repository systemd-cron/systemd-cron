#!/usr/bin/python3
import os
import sys
import time

try:
    delay = int(sys.argv[1]) * 60
except:
    print("Usage: %s <minutes>" % sys.argv[0])
    exit(1)

f = open('/proc/uptime', 'r')
uptime = int(float(f.readline().split()[0]))

if delay > uptime:
    time.sleep(delay - uptime)
