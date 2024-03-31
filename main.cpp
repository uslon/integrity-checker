#include <algorithm>
#include <cassert>
#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <thread>

#include <signal.h>
#include <syslog.h>

#include "crc32.h"

using crc32_t = uint32_t;
uint32_t crc32_table[256];


char* get_cli_option(char ** begin, char ** end, const std::string& option) {
    char ** itr = std::find(begin, end, option);
    if (itr != end && ++itr != end)
    {
        return *itr;
    }
    return nullptr;
}

char* get_option(int argc, char **argv, const std::string& option) {
    char* res = get_cli_option(argv, argv + argc, option);
    return res ? res : std::getenv(option.c_str() + 1); // erase '-'
}


struct integrity_state_t {
    crc32_t sum;

    enum class file_state_t {
        OK,
        NOT_FOUND,
        NOT_ACCESSIBLE,
        UNDEFINED_ERROR,
    };
    file_state_t file_state;

    std::string state_to_string() const {
        switch (file_state)
        {
        case file_state_t::OK:
            return "OK";
        case file_state_t::NOT_FOUND:
            return "NOT_FOUND";
        case file_state_t::NOT_ACCESSIBLE:
            return "NOT_ACCESSIBLE";
        case file_state_t::UNDEFINED_ERROR:
            return "UNDEFINED_ERROR";
        }
        assert(false); // not reachable
    }
};


integrity_state_t calculate_sum_on_file(const std::string& filename) {
    constexpr size_t buffer_size = 64 * 1024;
    char buffer[buffer_size];

    integrity_state_t res = {0, integrity_state_t::file_state_t::OK};
    std::FILE* file = std::fopen(filename.c_str(), "r");
    if (!file) {
        switch (errno)
        {
        case EACCES:
            res.file_state = integrity_state_t::file_state_t::NOT_ACCESSIBLE;
            break;
        case ENOENT:
            res.file_state = integrity_state_t::file_state_t::NOT_FOUND;
            break;
        default:
            res.file_state = integrity_state_t::file_state_t::UNDEFINED_ERROR;
            break;
        }
        return res;
    }

    while (size_t read_cnt = std::fread(buffer, sizeof(buffer[0]), buffer_size, file)) {
        res.sum = crc32::update(crc32_table, res.sum, buffer, read_cnt);
    }

    return res;
}

std::map<std::string, integrity_state_t> collect_states(const std::string& dir) {
    std::map<std::string, integrity_state_t> check_sums;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        check_sums[entry.path()] = calculate_sum_on_file(entry.path());
    }
    return check_sums;
}

void validate_sums(const std::map<std::string, integrity_state_t>& old_states,
    const std::map<std::string, integrity_state_t>& new_states) {
    auto it_old = old_states.begin();
    auto it_new = new_states.begin();

    bool ok = true;
    while (it_old != old_states.end() && it_new != new_states.end()) {
        if (it_old->first == it_new->first) {
            if (it_old->second.file_state == integrity_state_t::file_state_t::OK) {
                if (it_new->second.file_state != integrity_state_t::file_state_t::OK) {
                    syslog(LOG_ERR, "Integrity check: FAIL (%s - file is %s)\n", it_new->first.c_str(),
                        it_new->second.state_to_string().c_str());
                    ok = false;
                } else if (it_new->second.sum != it_old->second.sum) {
                    syslog(LOG_ERR, "Integrity check: FAIL (%s - check sums differ: <%x, %x>)\n", it_new->first.c_str(),
                        it_old->second.sum, it_new->second.sum);
                    ok = false;
                }
            } else if (it_new->second.file_state != it_old->second.file_state) {
                syslog(LOG_ERR, "Integrity check: FAIL (%s - file is %s)\n", it_new->first.c_str(),
                    it_new->second.state_to_string().c_str());
                ok = false;
            }
            ++it_old;
            ++it_new;
            continue;
        }

        if (it_old->first < it_new->first) {
            syslog(LOG_ERR, "Integrity check: FAIL (%s - file is NOT_FOUND)\n", it_old->first.c_str());
            ok = false;
            ++it_old;
        } else {
            ++it_new;
        }
    }

    while (it_old != old_states.end()) {
        syslog(LOG_ERR, "Integrity check: FAIL (%s - file is NOT_FOUND)\n", it_old->first.c_str());
        ok = false;
        ++it_old;
    }

    if (ok) {
        syslog(LOG_INFO, "Integrity check: OK\n");
    }
}

void do_check(const std::map<std::string, integrity_state_t>& initial_states, const std::string& dir) {
    auto current_states = collect_states(dir);
    validate_sums(initial_states, current_states);
}

void check_integrity(const std::string& dir, int check_interval) {
    std::map<std::string, integrity_state_t> initial_states = collect_states(dir);

    timer_t timerid;
    sigevent sev;
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGALRM;
    sev.sigev_value.sival_ptr = &timerid;
    if (timer_create(CLOCK_REALTIME, &sev, &timerid) == -1) {
        printf("timer_create failure, errno is %d\n", errno);
        syslog(LOG_ERR, "timer_create failure");
        exit(EXIT_FAILURE);
    }

    itimerspec its;
    its.it_value.tv_sec = check_interval;
    its.it_value.tv_nsec = 0;
    its.it_interval.tv_sec = its.it_value.tv_sec;
    its.it_interval.tv_nsec = 0;
    if (timer_settime(timerid, 0, &its, NULL) == -1) {
        printf("timer_settime failure, errno is %d\n", errno);
        syslog(LOG_ERR, "timer_settime failure");
        exit(EXIT_FAILURE);
    }

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGALRM);
    sigaddset(&set, SIGQUIT);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGHUP);
    if (pthread_sigmask(SIG_BLOCK, &set, NULL) != 0) {
        printf("pthread_sigmask failure, errno is %d\n", errno);
        syslog(LOG_ERR, "pthread_sigmask failure");
        exit(EXIT_FAILURE);
    }

    for (;;) {
        int sig;
        if (sigwait(&set, &sig) != 0) {
            syslog(LOG_ERR, "sigwait failure");
            exit(EXIT_FAILURE);
        }

        switch (sig)
        {
        case SIGUSR1:
        case SIGALRM:
            do_check(initial_states, dir);
            break;
        case SIGTERM:
            return;
        default:
            break;
        }
    }
}

int main(int argc, char **argv) {
    char* dir_arg = get_option(argc, argv, "-directory");
    if (!dir_arg) {
        printf("no target directory was found\n");
        exit(EXIT_FAILURE);
    }
    std::string directory(dir_arg);

    char* timer_arg = get_option(argc, argv, "-time_interval");
    if (!timer_arg) {
        printf("no timer duration was provided\n");
        exit(EXIT_FAILURE);
    }
    int check_interval = atoi(timer_arg);
    if (check_interval == 0) {
        printf("invalid timer duration was provided\n");
        exit(EXIT_FAILURE);
    }

    openlog(*argv, LOG_PID, 0);
    crc32::generate_table(crc32_table);
    check_integrity(directory, check_interval);
    closelog();
}
