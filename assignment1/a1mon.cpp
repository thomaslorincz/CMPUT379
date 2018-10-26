#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <sys/resource.h>
#include <signal.h>
#include <zconf.h>

static const int MAX_BUFFER = 256;

struct process {
    std::string user;
    std::string pid;
    std::string ppid;
    std::string state;
    std::string start;
    std::string cmd;
};

struct child {
    std::string pid;
    std::string cmd;
};

std::vector<process> ps_process_list;
std::vector<child> children;

/**
 * Determines if the process specified by target_pid is currently running
 * @param ps_process_list List of processes from ps
 * @param target_pid The pid of the target process
 * @return true if target_pid is found running, false otherwise
 */
bool is_running(std::string target_pid){
    for (auto &this_process : ps_process_list) {
        if (this_process.pid == target_pid) {
            return true;
        }
    }
    return false;
}

/**
 * Processes each line of the input buffer and creates processes out of them
 * @param buffer The command line input text buffer
 * @return A new process based on the ps line
 */
process process_line(const std::string &buffer) {
    process new_process;
    std::stringstream ss(buffer);
    std::string token;

    // Break the inputted line into 6 tokens
    int i = 0;
    while (std::getline(ss, token, ' ')) {
        if (token.length()) {
            if (i == 0) {
                new_process.user = token.c_str();
            } else if (i == 1) {
                new_process.pid = token.c_str();
            } else if (i == 2) {
                new_process.ppid = token.c_str();
            } else if (i == 3) {
                new_process.state = token.c_str();
            } else if (i == 4) {
                new_process.start = token.c_str();
            } else if (i == 5) {
                new_process.cmd = token.c_str();
            }
            i++;
        }
    }

    return new_process;
}

/**
 * Runs the ps process in the background to obtain command line input to parse
 * @return A list of processes
 */
std::vector<process> run_ps() {
    FILE *stream;
    char buffer[MAX_BUFFER];
    std::vector<process> output;

    // (b) Use popen to execute the ps program in the background
    stream = popen("ps -u $USER -o user,pid,ppid,state,start,cmd --sort start", "r");
    // (c) Read, display, and process each line produced by popen
    if (stream) {
        while (!feof(stream)) {
            if (fgets(buffer, MAX_BUFFER, stream) != NULL) {
                printf("%s", buffer); // Display line
                process new_process = process_line(buffer); // Process line
                output.push_back(new_process); // Add new process to list
            }
        }

        pclose(stream); // (c) Close the pipe
    }
    return output;
}

/**
 * Terminates all child processes spawned by the target process
 * @param children A list of child processes
 */
void terminate_children() {
    for (auto &this_child : children) {
        kill(std::stoi(this_child.pid), SIGKILL);
        printf("Terminated [pid= %s, cmd= %s]\n", this_child.pid.c_str(), this_child.cmd.c_str());
    }
}

/**
 * Gets a list of all child processes spawned by the target process
 * @param ps_process_list The list of processes
 * @param target_pid The pid of the target process
 * @return A list of child processes
 */
std::vector<child> get_children(std::string target_pid) {
    std::vector<child> output;
    printf("List of monitored processes:\n");
    for (auto &this_process : ps_process_list) {
        std::string ppid = this_process.ppid;
        if (ppid == target_pid) {
            std::string pid = this_process.pid;
            std::string cmd = this_process.cmd;
            printf("%lu: [%s, %s]\n", output.size(), pid.c_str(), cmd.c_str());
            child new_child = {
                .pid = pid,
                .cmd = cmd
            };
            output.push_back(new_child);
        }
    }

    return output;
}

/**
 * Main function. Loops continuously to monitor a target process and its child processes.
 * If the target process is terminated, its child processes will be terminated if they were not handled by the target
 * process itself.
 * @param argc Number of arguments
 * @param argv String of arguments
 * @return 0 if successful, 1 if error
 */
int main(int argc, char **argv) {
    // 1. Set a 10 minute CPU time limit
    rlimit time_limit {
        .rlim_cur = 600,
        .rlim_max = 600
    };
    setrlimit(RLIMIT_CPU, &time_limit);

    pid_t pid = getpid();

    unsigned int interval = 0; // Sleep interval
    unsigned int counter = 0; // Loop counter

    // A target pid must be specified
    if (argc < 2) {
        printf("Too few arguments\n");
        return 1;
    }

    char* target_pid = argv[1];

    // interval is an optional argument
    if (argc == 3) {
        interval = (unsigned int) strtol(argv[2], (char **) NULL, 10);
    } else {
        interval = 3; // Default interval = 3
    }

    if (argc > 3) {
        printf("Too many arguments\n");
        return 1;
    }

    // 2. Run the main loop of the program
    while (true) {
        // (a) Increment the iteration counter
        counter++;
        // (a) Print a header with counter, pid, target_pid, and interval
        printf("a1mon [counter= %u, pid= %i, target_pid= %s, interval= %u sec]:\n", counter, pid, target_pid, interval);

        // (b) Use popen to execute the ps program in the background
        ps_process_list = run_ps();

        // (d) Decide if target process is still running
        if (!is_running(target_pid)) {
            printf("a1mon: Target %s appears to have terminated. Cleaning up.\n", target_pid);
            // (d) Terminate child processes if not running
            terminate_children();
            // (d) The process then terminates
            return 0;
        }

        // Get and display the process's children
        children = get_children(target_pid);

        // (e) Delay the next iteration by interval seconds
        sleep(interval);
    }
}