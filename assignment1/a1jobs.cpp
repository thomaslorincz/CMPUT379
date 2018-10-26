#include <iostream>
#include <sys/resource.h>
#include <sys/times.h>
#include <sstream>
#include <iterator>
#include <vector>
#include <zconf.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <tuple>
#include <algorithm>

static const int MAX_JOBS = 32;

struct job {
    int index;
    pid_t pid;
    std::string cmd;
    bool running;
};

std::vector<job> job_list;

/**
 * List the spawned jobs that are running
 */
void list() {
    for (auto &this_job : job_list) {
        if (this_job.running) {
            printf("%i: (pid= %i, cmd= %s)\n", this_job.index, this_job.pid, this_job.cmd.c_str());
        }
    }
}

/**
 * Suspend the job corresponding to the given job number.
 * If not found, print an error message.
 * @param job_number The number of the job to suspend
 */
void suspend(int job_number) {
    try {
        if (!job_list.at(job_number).running) {
            printf("Job %i already terminated\n", job_number);
            return;
        }
        kill(job_list.at(job_number).pid, SIGSTOP);
        printf("Suspended job: %i\n", job_number);
    } catch (const std::out_of_range& _) {
        printf("ERROR: Failed to find job: %i - not suspending\n", job_number);
    }
}

/**
 * Resume the job corresponding to the given job number.
 * If not found, print an error message.
 * @param job_number The number of the job to resume
 */
void resume(int job_number) {
    try {
        if (!job_list.at(job_number).running) {
            printf("Job %i already terminated\n", job_number);
            return;
        }
        kill(job_list.at(job_number).pid, SIGCONT);
        printf("Resumed job: %i\n", job_number);
    } catch (const std::out_of_range& _) {
        printf("ERROR: Failed to find job: %i - not resuming\n", job_number);
    }
}

/**
 * Terminate the job corresponding to the given job number.
 * If not found, print an error message.
 * @param job_number The number of the job to terminate
 */
void terminate(int job_number) {
    try {
        if (!job_list.at(job_number).running) {
            printf("Job %i already terminated\n", job_number);
            return;
        }
        job_list.at(job_number).running = false;
        kill(job_list.at(job_number).pid, SIGKILL);
        printf("Killed job: %i\n", job_number);
    } catch (const std::out_of_range& _) {
        printf("ERROR: Invalid job number: %i - not terminating\n", job_number);
    }
}

/**
 * Terminate all spawned jobs
 */
void terminate_all() {
    for (auto &this_job : job_list) {
        if (this_job.running) {
            kill(this_job.pid, SIGKILL);
            printf("Terminated job: %i (pid= %i)\n", this_job.index, this_job.pid);
        }
    }
}

/**
 * Main function. Loops continuously to monitor the process corresponding to the inputted target_pid.
 * @param argc Number of arguments
 * @param argv Array of argument strings
 * @return 0 upon successful exit of the program
 */
int main(int argc, char *argv[]) {
    // 1. Set a 10 minute CPU time limit
    rlimit time_limit {
        .rlim_cur = 600,
        .rlim_max = 600
    };
    setrlimit(RLIMIT_CPU, &time_limit);

    int job_idx = 0;

    // 2. Call function times() to record the user and CPU start times
    tms start_cpu;
    clock_t start_time = times(&start_cpu);

    // Get this process's pid
    pid_t pid = getpid();

    // 3. Run the main loop of the program
    while (true) {
        std::string cmd;

        // Get the current command input
        std::cin.clear();
        printf("a1jobs[%i]: ", pid);
        std::getline(std::cin, cmd);

        // Tokenize the command input (space delimited)
        std::istringstream iss(cmd);
        std::vector<std::string> tokens {
            std::istream_iterator<std::string>{iss},
            std::istream_iterator<std::string>{}
        };

        if (tokens.empty()) {
            printf("No command inputted\n");
        } else if (tokens.at(0) == "list") {
            // List all jobs that have not been explicitly terminated by the user
            list();
        } else if (tokens.at(0) == "run") {
            if (job_idx < MAX_JOBS) {
                pid_t c_pid = fork();
                errno = 0;

                if (c_pid == 0) {
                    switch (tokens.size()) {
                        case 1:
                            printf("Too few argument to run\n");
                            break;
                        case 2:
                            execlp(tokens.at(1).c_str(), tokens.at(1).c_str(), (char *) nullptr);
                            break;
                        case 3:
                            execlp(tokens.at(1).c_str(), tokens.at(1).c_str(), tokens.at(2).c_str(), (char *) nullptr);
                            break;
                        case 4:
                            execlp(tokens.at(1).c_str(), tokens.at(1).c_str(), tokens.at(2).c_str(),
                                tokens.at(3).c_str(), (char *) nullptr);
                            break;
                        case 5:
                            execlp(tokens.at(1).c_str(), tokens.at(1).c_str(), tokens.at(2).c_str(),
                                tokens.at(3).c_str(), tokens.at(4).c_str(), (char *) nullptr);
                            break;
                        case 6:
                            execlp(tokens.at(1).c_str(), tokens.at(1).c_str(), tokens.at(2).c_str(),
                                tokens.at(3).c_str(), tokens.at(4).c_str(), tokens.at(5).c_str(), (char *) nullptr);
                            break;
                        default:
                            printf("Too many arguments to run\n");
                            break;
                    }

                    return 0;
                }

                if (errno) {
                    printf("ERROR: %s\n", strerror(errno));
                    errno = 0;
                } else {
                    job new_job = {
                        .index = job_idx,
                        .pid = c_pid,
                        .cmd = tokens.at(1),
                        .running = true
                    };
                    job_list.push_back(new_job);
                    job_idx++;
                }
            } else {
                printf("Too many jobs running\n");
            }
        } else if (tokens.at(0) == "suspend") {
            if (tokens.size() < 2) {
                printf("ERROR: No job number specified\n");
            } else {
                int job_number = std::stoi(tokens.at(1), nullptr, 10);
                suspend(job_number);
            }
        } else if (tokens.at(0) == "resume") {
            if (tokens.size() < 2) {
                printf("ERROR: No job number specified\n");
            } else {
                int job_number = std::stoi(tokens.at(1), nullptr, 10);
                resume(job_number);
            }
        } else if (tokens.at(0) == "terminate") {
            if (tokens.size() < 2) {
                printf("ERROR: No job number specified\n");
            } else {
                int job_number = std::stoi(tokens.at(1), nullptr, 10);
                terminate(job_number);
            }
        } else if (tokens.at(0) == "exit") {
            terminate_all();
            break;
        } else if (tokens.at(0) == "quit") {
            printf("WARNING: Exiting a1jobs without terminating head processes\n");
            break;
        } else {
            printf("ERROR: Invalid input\n");
        }
    }

    // Call function times() to record the user and CPU end times
    tms end_cpu;
    clock_t end_time = times(&end_cpu);

    // Calculate and print recorded times
    printf("Real time: %li sec\n", (long int) (end_time - start_time) / sysconf(_SC_CLK_TCK));
    printf("User time: %li sec\n", (long int) (end_cpu.tms_utime - start_cpu.tms_utime) / sysconf(_SC_CLK_TCK));
    printf("Sys time: %li sec\n", (long int) (end_cpu.tms_stime - start_cpu.tms_stime) / sysconf(_SC_CLK_TCK));
    printf("Child user time: %li sec\n", (long int) (end_cpu.tms_cutime - start_cpu.tms_cutime) / sysconf(_SC_CLK_TCK));
    printf("Child sys time: %li sec\n", (long int) (end_cpu.tms_cstime - start_cpu.tms_cstime) / sysconf(_SC_CLK_TCK));

    return 0;
}