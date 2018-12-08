#include <stdio.h>
#include <iostream>
#include <unistd.h>
#include <vector>
#include <fstream>
#include <map>
#include <sys/resource.h>
#include <sys/times.h>
#include <cstring>
#include <pthread.h>

#define NTASKS 25
#define MAX_NAME_LEN 32
#define MAX_FILENAME_LEN 255
#define MAX_LINE_LEN 200

using namespace std;

typedef enum {WAIT, RUN, IDLE} task_status;

typedef struct {
    bool isAssigned;
    int timesExecuted;
    task_status status;
    long busyTime;
    long idleTime;
    long totalBusyTime;
    long totalIdleTime;
    long totalWaitTime;
    char name[MAX_NAME_LEN];
    vector<string> requiredResources;
} Task;

// Global variables
int nIter = 0; // The simulator finishes when each task in the system executes nIter times
long clockTickRate = 0; // Clock ticks per second - used in printing timing information
map<string, int> resources; // Global map of resource names to resource amounts
vector<Task> taskList; // Global list of tasks
pthread_mutex_t threadMutex, iterationMutex, monitorMutex; // Mutexes
pthread_t taskThreadList[NTASKS]; // Global list of task threads

/**
 * A utility function for converting string inputs into integers. Handles strtol() failures.
 */
int strToInt(char *stringInput) {
  int output = (int) strtol(stringInput, (char **) nullptr, 10);
  if (errno) {
    printf("Invalid strToInt input: %s\n", stringInput);
    perror("strtol() failure");
    exit(errno);
  }
  return output;
}

/**
 * Suspend the execution of the calling thread.
 */
void delay(long milliseconds) {
  struct timespec interval{
      .tv_sec = milliseconds / 1000,
      .tv_nsec = (milliseconds % 1000) * 1000000
  };
  nanosleep(&interval, nullptr);
  if (errno) {
    perror("nanosleep() failure");
    exit(errno);
  }
}

/**
 * Utility function for initializing a new pthread mutex.
 */
void initMutex(pthread_mutex_t *mutex) {
  int error = pthread_mutex_init(mutex, nullptr);
  if (error) {
    printf("Error: initMutex() (Error code = %i)\n", error);
    exit(EXIT_FAILURE);
  }
}

/**
 * Utility function for locking a pthread mutex.
 */
void lockMutex(pthread_mutex_t *mutex) {
  int error = pthread_mutex_lock(mutex);
  if (error) {
    printf("Error: lockMutex() (Error code = %i)\n", error);
    exit(EXIT_FAILURE);
  }
}

/**
 * Utility function for unlocking a pthread mutex.
 */
void unlockMutex(pthread_mutex_t *mutex) {
  int error = pthread_mutex_unlock(mutex);
  if (error) {
    printf("Error: unlockMutex() (Error code = %i)\n", error);
    exit(EXIT_FAILURE);
  }
}

/**
 * Prints the current status of all tasks.
 */
void monitorPrint() {
  string waitString;
  string runString;
  string idleString;

  for (auto &task : taskList) {
    if (task.status == WAIT) {
      waitString.append(task.name);
      waitString.append(" ");
    } else if (task.status == RUN) {
      runString.append(task.name);
      runString.append(" ");
    } else if (task.status == IDLE) {
      idleString.append(task.name);
      idleString.append(" ");
    } else {
      printf("Error: Unknown task status %i\n", task.status);
      exit(EXIT_FAILURE);
    }
  }

  printf("Monitor: [WAIT] %s\n\t [RUN] %s\n\t [IDLE] %s\n\n", waitString.c_str(), runString.c_str(),
         idleString.c_str());
}

/**
 * Monitor thread that prints out details periodically.
 */
void *monitorThread(void *arg) {
  while (true) {
    delay((long) arg);
    lockMutex(&monitorMutex);
    monitorPrint();
    unlockMutex(&monitorMutex);
  }
}

/**
 * Parse a resource line from the input file.
 */
void parseResourceLine(char *resourceLine) {
  vector<char*> resourceTokens;

  char line[MAX_LINE_LEN];
  strcpy(line, resourceLine);

  // Parse all the name:value pairs
  char *token = strtok(nullptr, " ");
  while (token != nullptr) {
    resourceTokens.push_back(token);
    token = strtok(nullptr, " ");
  }

  for (auto &resourceToken : resourceTokens) {
    string resourceName(strtok(resourceToken, ":"));
    resources[resourceName] = strToInt(strtok(nullptr, ":"));
  }
}

/**
 * Parse and initialize resources and tasks from a given input file.
 */
void parseTaskFile(char *taskFilePath) {
  string lineString;

  ifstream file(taskFilePath);
  if (file.fail()) {
    printf("Error: File does not exist.\n");
    exit(EXIT_FAILURE);
  }

  if (file.good()) {
    while (getline(file, lineString)) {
      // Ignore comments and whitespace
      if (lineString[0] == '#' || lineString[0] == '\r' || lineString[0] == '\n') continue;

      char line[MAX_LINE_LEN];
      strcpy(line, lineString.c_str());

      char *lineType = strtok(line, " ");
      if (!(strcmp(lineType, "resources") == 0 || strcmp(lineType, "task") == 0)) {
        printf("Error: Unknown line type %s. Expected 'resource' or 'task'.\n", lineType);
        exit(EXIT_FAILURE);
      }

      if (strcmp(lineType, "resources") == 0) {
        strcpy(line, lineString.c_str());
        parseResourceLine(line);
      } else {
        Task newTask;

        strcpy(newTask.name, strtok(nullptr, " "));
        newTask.busyTime = strtol(strtok(nullptr, " "), (char **) nullptr, 10);
        newTask.idleTime = strtol(strtok(nullptr, " "), (char **) nullptr, 10);
        newTask.isAssigned = false;
        newTask.status = IDLE;
        newTask.totalIdleTime = 0;
        newTask.totalBusyTime = 0;
        newTask.totalWaitTime = 0;
        newTask.timesExecuted = 0;

        char *resource = strtok(nullptr, " ");

        // Add resource strings to list
        while (resource != nullptr) {
          string str(resource);
          newTask.requiredResources.push_back(str);
          resource = strtok(nullptr, " ");
        }

        taskList.push_back(newTask);
      }
    }
  }
}

/**
 * Determines if the Task has all the resources it needs to run.
 */
bool hasEnoughResources(Task *task) {
  for (auto &reqResource : task->requiredResources) {
    char resource[MAX_NAME_LEN];
    strcpy(resource, reqResource.c_str());
    char *resourceName = strtok(resource, ":");

    if (resources[resourceName] < strToInt(strtok(nullptr, ":"))) return false;
  }
  return true;
}

/**
 * Sets a task to procure and use a resource. Decrements the appropriate resources when called.
 */
void assignResources(Task *task) {
  for (auto &reqResource : task->requiredResources) {
    char resource[MAX_NAME_LEN];
    strcpy(resource, reqResource.c_str());
    char *resourceName = strtok(resource, ":");
    resources[resourceName] -= strToInt(strtok(nullptr, ":"));
  }
}

/**
 * Return the amount of resources used by the Task back to the global resource
 * list.
 */
void returnResources(Task *task) {
  for (auto &reqResource : task->requiredResources) {
    char resource[MAX_NAME_LEN];
    strcpy(resource, reqResource.c_str());
    char *resourceName = strtok(resource, ":");
    resources[resourceName] += strToInt(strtok(nullptr, ":"));
  }
}

/**
 * Run Task iterations while using controller logic to avoid race conditions.
 */
void runIterations(Task *task) {
  int iterationCounter = 0;
  clock_t waitStart, waitFinish;
  struct tms tmswaitstart{}, tmswaitend{};

  lockMutex(&monitorMutex);
  task->status = WAIT;
  unlockMutex(&monitorMutex);

  waitStart = times(&tmswaitstart);
  while (true) {
    lockMutex(&iterationMutex);

    if (!hasEnoughResources(task)) {
      unlockMutex(&iterationMutex);
      delay(100); // Slight delay before continuing
      continue;
    }

    // Assign resources to task from shared resource pool
    assignResources(task);
    waitFinish = times(&tmswaitend);
    task->totalWaitTime += ((waitFinish - waitStart) / clockTickRate) * 1000;
    unlockMutex(&iterationMutex);

    // After resources are taken, simulate the execution of the process
    lockMutex(&monitorMutex);
    task->status = RUN;
    unlockMutex(&monitorMutex);
    delay(task->busyTime);
    task->totalBusyTime += task->busyTime;

    // After running the busy time then return the resources back to the pool
    lockMutex(&iterationMutex);
    returnResources(task);
    unlockMutex(&iterationMutex);

    // Wait for idle time and increment iteration counter
    lockMutex(&monitorMutex);
    task->status = IDLE;
    unlockMutex(&monitorMutex);

    delay(task->idleTime);
    task->totalIdleTime += task->idleTime;
    iterationCounter += 1;
    task->timesExecuted += 1;

    if (iterationCounter == nIter) return;

    lockMutex(&monitorMutex);
    task->status = WAIT;
    unlockMutex(&monitorMutex);
    waitStart = times(&tmswaitstart);
  }
}

/**
 * Assigns a task to a new thread and executes the task.
 */
void *executeThread(void *arg) {
  taskThreadList[(long)arg] = pthread_self();

  // Iterate through the task list and assign an unassigned task to this thread
  for (auto &task : taskList) {
    if (task.isAssigned) continue;
    task.isAssigned = true;
    unlockMutex(&threadMutex); // Release mutex for next thread and jump to main loop
    runIterations(&task);
    break;
  }

  pthread_exit(nullptr);
}

/**
 * After all other threads finish, the main thread prints information about system resources and
 * system tasks according to the format in the assignment specification.
 */
void printTerminationInfo() {
  map<string, int>::iterator itr;

  printf("System Resources:\n");
  for (itr = resources.begin(); itr != resources.end(); itr++) {
    printf("\t\t%s: (maxAvail=\t%d, held=\t0)\n", (itr->first).c_str(), resources[itr->first]);
  }
  printf("\n");

  printf("System Tasks:\n");
  for (unsigned long i = 0; i < taskList.size(); i++) {
    string status;
    if (taskList.at(i).status == IDLE) {
      status = "IDLE";
    } else if (taskList.at(i).status == WAIT) {
      status = "WAIT";
    } else {
      status = "RUN";
    }

    printf("[%lu] %s (%s, runTime= %lu msec, idleTime= %lu msec):\n", i, taskList.at(i).name,
           status.c_str(), taskList.at(i).totalBusyTime, taskList.at(i).totalIdleTime);
    printf("\t (tid= %lu\n", taskThreadList[i]);

    // Print the required resources
    for (auto &reqResource : taskList.at(i).requiredResources) {
      char resourceString[MAX_NAME_LEN];
      strcpy(resourceString, reqResource.c_str());

      char *resourceName = strtok(resourceString, ":");
      printf("\t %s: (needed=\t%d, held= 0)\n", resourceName, strToInt(strtok(nullptr, ":")));
    }

    printf("\t (RUN: %d times, WAIT: %lu msec\n\n", taskList.at(i).timesExecuted,
           taskList.at(i).totalWaitTime);
  }
}

int main(int argc, char **argv) {
  // Set a 10 minute CPU time limit
  rlimit timeLimit{.rlim_cur = 600, .rlim_max = 600};
  setrlimit(RLIMIT_CPU, &timeLimit);

  if (argc != 4) {
    printf("Error: Invalid arguments. Expected 'a4tasks inputFile monitorTime nIter'\n");
    exit(EXIT_FAILURE);
  }

  // Parse input file name
  string inputFile = argv[1];
  char fileName[MAX_FILENAME_LEN];
  strcpy(fileName, inputFile.c_str());

  // Parse monitor time interval
  long monitorTime = strtol(argv[2], (char **) nullptr, 10);
  if (errno) {
    perror("Invalid monitorTime: strtol() failure");
    return errno;
  }

  // Parse number of task iterations
  nIter = (uint) strtol(argv[3], (char **) nullptr, 10);
  if (errno) {
    perror("Invalid nIter: strtol() failure");
    return errno;
  }

  initMutex(&threadMutex);
  initMutex(&iterationMutex);
  initMutex(&monitorMutex);

  // Get number of clock cycles per second. Used for timing functions
  clockTickRate = sysconf(_SC_CLK_TCK);
  if (errno) {
    perror("sysconf() failure");
    return errno;
  }

  parseTaskFile(fileName); // Parse task file and populate global lists

  pthread_t newPthreadId;

  // Create a monitor pthread
  pthread_create(&newPthreadId, nullptr, monitorThread, (void *) monitorTime);
  if (errno) {
    perror("Monitor pthread creation error: pthread_create() failure");
    exit(errno);
  }

  // Create a new pthread for every task in the task list
  for (unsigned long i = 0; i < taskList.size(); i++) {
    lockMutex(&threadMutex);
    pthread_create(&newPthreadId, nullptr, executeThread, (void *) i);
    if (errno) {
      perror("Task list pthread creation error: pthread_create() failure");
      exit(errno);
    }
  }

  delay(500); // Give delay before waiting on threads

  // Wait for all other threads to complete before continuing
  for (unsigned long i = 0; i < taskList.size(); i++) {
    pthread_join(taskThreadList[i], nullptr);
    if (errno) {
      perror("pthread_join() failure");
      exit(errno);
    }
  }

  printTerminationInfo();

  return EXIT_SUCCESS;
}