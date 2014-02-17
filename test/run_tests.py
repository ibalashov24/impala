#!/usr/bin/env python

"""
This script can be used to execute the tests.

Usage: run_tests.py [options] <subdirectory that contains test cases>

Command line options:
 -e, --executable <Path to executable>
 --disable-progressbar Disable the fancy progress bar
"""

import infrastructure.tests
import os, sys, getopt, imp

def invoke(executable, directory, pb):
    testfile = os.path.join(directory, "tests.py")
    
    if os.path.exists(testfile):
        tests = imp.load_source("tests", testfile).allTests()
    else:
        tests = infrastructure.tests.make_tests(directory)
        
    infrastructure.tests.executeTests(tests, executable, pb)

def main():
    executable = "../build/bin/impala"
    pb = True
    
    if len(sys.argv) < 2:
        print("You did not specify a test directory.")
        print(__doc__)
        sys.exit(2)
    else:
        directory = sys.argv[-1]
    
    # get cmd file
    try:
        opts, args = getopt.getopt(sys.argv[1:-1], "he:", ["help", "executable", "disable-progressbar"])
    except getopt.error as msg:
        print(msg)
        sys.exit(2)
    
    # handle options
    for o, a in opts:
        if o in ("-h", "--help"):
            print(__doc__)
            sys.exit(0)
        if o in ("-e", "--executable"):
            executable = a
        if o in ("--disable-progressbar"):
            pb = False

    invoke(executable, directory, pb)

main()
