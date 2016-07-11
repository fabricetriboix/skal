#!/usr/bin/env python

import platform

def autodetectplf():
    if platform.machine() in ["x86_64", "AMD64"]:
        arch = "x64"
    elif platform.machine()[0:3] == 'arm':
        arch = "arm"
    else:
        arch = platform.machine()

    if platform.system() == "Linux":
        OS = "linux"
    elif platform.system() == "Windows":
        OS = "win32"
    else:
        OS = platform.system()

    return "{}-{}".format(arch, OS)

if __name__ == '__main__':
    print(autodetectplf())
