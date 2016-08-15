#!/usr/bin/env python

# Scan code to show TODO list

import os
from fnmatch import fnmatch

# Find all the source files
unitTests = []
for dirName, dirs, files in os.walk("."):
    for filename in files:
        if fnmatch(filename, "*.c") or fnmatch(filename, "*.cpp") \
                or fnmatch(filename, "SCons*"):
            path = os.path.join(dirName, filename)
            f = open(path, "r")
            lineno = 0
            for line in f:
                lineno += 1
                if "TODO" in line or "XXX" in line or "FIXME" in line:
                    print("[{}:{}] {}".format(path, lineno, line.strip()))
            f.close()
