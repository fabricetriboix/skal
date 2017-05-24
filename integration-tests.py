#!/usr/bin/env python

import sys
import os
import time
import argparse
import logging
import threading
import signal
import subprocess

# Parse arguments
parser = argparse.ArgumentParser(description="Run SKAL system tests")
parser.add_argument("-v", "--verbose", default=False, action="store_true",
        help="Be verbose")
args = parser.parse_args()

# Setup logger
formatter = logging.Formatter("%(levelname)s: %(message)s")
handler = logging.StreamHandler()
handler.setFormatter(formatter)
logger = logging.getLogger(__name__)
logger.addHandler(handler)
if args.verbose:
    logger.setLevel(logging.DEBUG)
else:
    logger.setLevel(logging.WARNING)

# List of subprocesses currently started
proc = dict()

def terminateAll():
    for name, process in proc.items():
        logger.debug("Terminating process '{}'".format(name))
        process.terminate()
    time.sleep(0.2)
    for name, process in proc.items():
        if process.poll() is None:
            logger.debug("Process '{}' is still alive; killing it!".format(
                    name))
            process.kill()
    proc.clear()

# Cleanup function
def sigtermHandler(signum, frame):
    logger.warning("Signal caught; cleaning up")
    terminateAll()
    sys.exit(1)

signal.signal(signal.SIGTERM, sigtermHandler)
signal.signal(signal.SIGINT, sigtermHandler)

# Timeout handler
# NB: `sys.exit()` only exits the threads that runs the timer
def timeout():
    print("Timeout!")
    os.kill(os.getpid(), signal.SIGTERM)

# Wrapper around `subprocess.Popen()`
outputFiles = dict()
outputPaths = dict()
def spawn(arguments):
    if not os.sep in arguments[0]:
        arguments[0] = os.path.join(".", arguments[0])
    procname = os.path.basename(arguments[0])
    if outputFiles.get(procname) is None:
        outputPaths[procname] = procname + ".log"
        outputFiles[procname] = open(outputPaths[procname], 'w')
    return subprocess.Popen(arguments,
            stdout=outputFiles[procname], stderr=subprocess.STDOUT)

# Keep track of the results
results = list()

# Test runner
def runTest(description, timeout_s, body):
    timer = threading.Timer(timeout_s, timeout)
    proc.clear()
    logger.info(description)
    timer.start()
    success = body()
    timer.cancel()
    terminateAll()
    results.append((description, success))


# Utility function to start skald, one reader and one writer
def startSkaldReaderWriter(socketUrl, argsSkald, argsReader, argsWriter):
    logger.debug("Starting SKALD")
    url = "unix://skald.sock"
    proc['skald'] = spawn(["skald", "-u", socketUrl] + argsSkald)
    time.sleep(0.01)
    logger.debug("Starting reader process")
    proc['reader'] = spawn(["reader", "-u", socketUrl] + argsReader)
    time.sleep(0.01)
    logger.debug("Starting writer process")
    proc['writer'] = spawn(["writer", "-u", socketUrl] + argsWriter)


def testSendOneMsg():
    startSkaldReaderWriter("unix://skald.sock",
            ["-d", "TestDomain"], [], ["-c", "1", "reader"])
    logger.debug("Wait for reader process to finish")
    proc['reader'].wait()
    del(proc['reader'])
    return True

runTest("Send one message through SKALD", 0.5, testSendOneMsg)


def testSendFiveMsg():
    startSkaldReaderWriter("unix://skald.sock",
            ["-d", "TestDomain"], [], ["-c", "5", "reader"])
    logger.debug("Wait for reader process to finish")
    proc['reader'].wait()
    del(proc['reader'])
    return True

runTest("Send five messages through SKALD", 0.5, testSendFiveMsg)


def testSendOneMsgToGroup():
    startSkaldReaderWriter("unix://skald.sock",
            ["-d", "TestDomain"], ["-m", "TestGr"], ["-c", "1", "-m", "TestGr"])
    logger.debug("Wait for reader process to finish")
    proc['reader'].wait()
    del(proc['reader'])
    return True

runTest("Send one message to a multicast group", 0.5, testSendOneMsgToGroup)


def testSendFiveMsgToGroup():
    startSkaldReaderWriter("unix://skald.sock",
            ["-d", "TestDomain"], ["-m", "TestGr"], ["-c", "5", "-m", "TestGr"])
    logger.debug("Wait for reader process to finish")
    proc['reader'].wait()
    del(proc['reader'])
    return True

runTest("Send five messages to a multicast group", 0.5, testSendFiveMsgToGroup)


# Check for memory leaks
memoryLeak = False
for p, f in outputFiles.items():
    f.close()
for p, filepath in outputPaths.items():
    with open(filepath) as f:
        for line in f:
            if "Memory leak detected" in line:
                print("Memory leak detected for process '{}'; "
                        "please refer to file '{}'".format(p, filepath))
                memoryLeak = True
                break
        f.close()

# Print the results
failCount = 0
for description, success in results:
    if not success:
        failCount += 1
if failCount > 0:
    print("INTEGRATION TEST FAIL - {}/{} integration tests failed".format(
            failCount, len(results)))
    sys.exit(1)
else:
    if memoryLeak:
        print("INTEGRATION TEST FAIL - {0}/{0} integration tests passed, "
                "but with memory leaks".format(len(results)))
        sys.exit(1)
    print("INTEGRATION TEST PASS - {0}/{0} integration tests passed".format(
            len(results)))
