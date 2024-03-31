#!/usr/bin/env python3
import os
import signal
import subprocess
import time
import unittest

TEXT_PATH = "text.txt"

def get_last_syslog():
    syslog = subprocess.Popen(("cat", "/var/log/syslog"), stdout=subprocess.PIPE)
    grep = subprocess.Popen(("grep", "integrity"), stdin=syslog.stdout, stdout=subprocess.PIPE)
    last_entry = subprocess.check_output(("tail", "-n", "1"), stdin=grep.stdout).decode("utf-8")
    return last_entry

def do_cleanup(pid):
    try:
        os.chmod(TEXT_PATH, 777)
        os.remove(TEXT_PATH)
    except:
        pass

    os.kill(pid, signal.SIGTERM)
    os.waitpid(pid, 0)

class Testing(unittest.TestCase):
    def test_all(self):
        subprocess.run(['make'])

        with open(TEXT_PATH, "w+") as text_file:
            text_file.write("Test text.");

        pwd = os.getcwd()
        binary_path = os.path.join(pwd, "integrity_checker")
        daemon = subprocess.Popen([binary_path, "-time_interval", "2", "-directory", pwd])
        self.addClassCleanup(do_cleanup, daemon.pid)

        time.sleep(2.2)

        last_entry = get_last_syslog().split()
        binary_in_log = binary_path + "[" + str(daemon.pid) + "]:"
        self.assertTrue(binary_in_log in last_entry)
        self.assertTrue("OK" == last_entry[-1][:2])


        with open(TEXT_PATH, "w+") as text_file:
            text_file.write("Another test text.")
        os.kill(daemon.pid, signal.SIGUSR1)
        time.sleep(0.5)

        last_entry = get_last_syslog().split()
        self.assertTrue("FAIL" in last_entry)
        self.assertTrue(binary_in_log in last_entry)
        self.assertTrue("check" in last_entry)
        self.assertTrue("sums" in last_entry)
        self.assertTrue("differ:" in last_entry)


        os.chmod(TEXT_PATH, 000)
        os.kill(daemon.pid, signal.SIGUSR1)
        time.sleep(0.5)

        last_entry = get_last_syslog().split()
        self.assertTrue("FAIL" in last_entry)
        self.assertTrue(binary_in_log in last_entry)
        self.assertTrue("file" in last_entry)
        self.assertTrue("is" in last_entry)
        self.assertTrue("NOT_ACCESSIBLE)" in last_entry)


        os.remove(TEXT_PATH)
        os.kill(daemon.pid, signal.SIGUSR1)
        time.sleep(0.5)

        last_entry = get_last_syslog().split()
        self.assertTrue("FAIL" in last_entry)
        self.assertTrue(binary_in_log in last_entry)
        self.assertTrue("file" in last_entry)
        self.assertTrue("is" in last_entry)
        self.assertTrue("NOT_FOUND)" in last_entry)


if __name__ == '__main__':

    unittest.main()
