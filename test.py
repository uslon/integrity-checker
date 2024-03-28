#!/usr/bin/env python3
import os
import signal
import subprocess
import time


def get_last_syslog():
    syslog = subprocess.Popen(("cat", "/var/log/syslog"), stdout=subprocess.PIPE)
    grep = subprocess.Popen(("grep", "integrity"), stdin=syslog.stdout, stdout=subprocess.PIPE)
    last_entry = subprocess.check_output(("tail", "-n", "1"), stdin=grep.stdout).decode("utf-8")
    return last_entry

def main():
    subprocess.run(['make'])

    text_path = "text.txt"
    with open(text_path, "w+") as text_file:
        text_file.write("Test text.");

    pwd = os.getcwd()
    binary_path = os.path.join(pwd, "integrity_checker")
    daemon = subprocess.Popen([binary_path, "-time_interval", "2", "-directory", pwd])

    time.sleep(2.2)

    last_entry = get_last_syslog().split()
    binary_in_log = binary_path + "[" + str(daemon.pid) + "]:"
    assert (binary_in_log in last_entry)
    assert ("OK" == last_entry[-1][:2])


    with open(text_path, "w+") as text_file:
        text_file.write("Another test text.")
    os.kill(daemon.pid, signal.SIGUSR1)
    time.sleep(0.5)

    last_entry = get_last_syslog().split()
    assert ("FAIL" in last_entry)
    assert (binary_in_log in last_entry)
    assert ("check" in last_entry)
    assert ("sums" in last_entry)
    assert ("differ:" in last_entry)


    os.chmod(text_path, 000)
    os.kill(daemon.pid, signal.SIGUSR1)
    time.sleep(0.5)

    last_entry = get_last_syslog().split()
    assert ("FAIL" in last_entry)
    assert (binary_in_log in last_entry)
    assert ("file" in last_entry)
    assert ("is" in last_entry)
    assert ("NOT_ACCESSIBLE)" in last_entry)


    os.remove(text_path)
    os.kill(daemon.pid, signal.SIGUSR1)
    time.sleep(0.5)

    last_entry = get_last_syslog().split()
    assert ("FAIL" in last_entry)
    assert (binary_in_log in last_entry)
    assert ("file" in last_entry)
    assert ("is" in last_entry)
    assert ("NOT_FOUND)" in last_entry)


    os.kill(daemon.pid, signal.SIGTERM)
    os.waitpid(daemon.pid, 0)

    print("test is OK!")


if __name__ == '__main__':
    main()
