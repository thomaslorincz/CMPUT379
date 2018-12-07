#include <stdio.h>
#include <iostream>
#include <unistd.h>
#include <vector>
#include <fstream>
#include <map>
#include <sys/times.h>
#include <cstring>
#include <pthread.h>

#define NTASKS 25
#define MAX_NAME_LEN 32
#define MAX_FILENAME_LEN 255

using namespace std;

typedef enum {WAIT, RUN, IDLE} Status;

typedef struct {
    bool isAssigned;
    int busyTime;
    int idleTime;
    int timesExecuted;
    Status status;
    long totalBusyTime;
    long totalIdleTime;
    long totalWaitTime;
    char name[MAX_NAME_LEN];
    vector<string> requiredResources;
} Task;

map<string, int> resources;
vector<Task> taskList;

pthread_mutex_t threadMutex;
pthread_mutex_t iterationMutex;
pthread_mutex_t monitorMutex; // Used for monitor to prevent states from switching
pthread_t taskThreadList[NTASKS];

int nIter = 0; // The simulator finishes when each task in the system executes nIter times
static long clktck = 0;

/**
 * A utility function for converting string inputs into integers. Handles strtol() failures.
 */
int strToInt(char *stringInput) {
  int output = (int) strtol(stringInput, (char **) nullptr, 10);
  if (errno) {
    printf("Invalid input: %s\n", stringInput);
    perror("strtol() failure");
    exit(errno);
  }
  return output;
}

/**
 * TODO
 */
void delay(long milliseconds) {
  struct timespec interval{
      .tv_sec = milliseconds / 1000,
      .tv_nsec = (milliseconds % 1000) * 1000000
  };
  nanosleep(&interval, nullptr);
  if (errno) {
    perror("nanosleep() failure: unable to set delay");
  }
}

/**
 * TODO
 */
void initMutex(pthread_mutex_t *mutex) {
  int error = pthread_mutex_init(mutex, nullptr);
  if (error) {
    printf("Error: initMutex() (Error code = %i)\n", error);
    exit(EXIT_FAILURE);
  }
}

/**
 * TODO
 */
void lockMutex(pthread_mutex_t *mutex) {
  int error = pthread_mutex_lock(mutex);
  if (error) {
    printf("Error: lockMutex() (Error code = %i)\n", error);
    exit(EXIT_FAILURE);
  }
}

/**
 * TODO
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
    } else if(task.status == RUN) {
      runString.append(task.name);
      runString.append(" ");
    } else {
      idleString.append(task.name);
      idleString.append(" ");
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
 * Parse out a resource line. (e.g. resources a:1 b:2)
 */
void parseResourceLine(char *resourceLine) {
  char *temp;
  char line[100];
  vector<char*> resourceStrings;

  strcpy(line, resourceLine);

  // Parse all the name:value pairs
  temp = strtok(nullptr, " ");
  while (temp != nullptr) {
    resourceStrings.push_back(temp);
    temp = strtok(nullptr, " ");
  }

  for (auto &resourceString : resourceStrings) {
    string tempName(strtok(resourceString, ":"));
    resources[tempName] = strToInt(strtok(nullptr, ":"));
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

      char cline[100];
      strcpy(cline, lineString.c_str());

      char *lineType = strtok(cline, " ");
      if (!(strcmp(lineType, "resources") == 0 || strcmp(lineType, "task") == 0)) {
        printf("Error: Unknown line type %s. Expected 'resource' or 'task'.\n", lineType);
        exit(EXIT_FAILURE);
      }

      if (strcmp(lineType, "resources") == 0) {
        // we need to define the resources that will be used
        strcpy(cline, lineString.c_str());
        parseResourceLine(cline);
      } else {
        Task newTask;

        strcpy(newTask.name, strtok(nullptr, " "));
        newTask.busyTime = (int) strtol(strtok(nullptr, " "), (char **) nullptr, 10);
        newTask.idleTime = (int) strtol(strtok(nullptr, " "), (char **) nullptr, 10);
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
 * Called by a task needs to check if their are resources up for grabs. If none are the task goes
 * back to waiting.
 */
bool hasEnoughResources(Task *task) {
  for (auto &reqResource : task->requiredResources) {
    char resource[MAX_NAME_LEN];
    strcpy(resource, reqResource.c_str());
    char *resName = strtok(resource, ":");

    if (resources[resName] < strToInt(strtok(nullptr, ":"))) return false;
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
 * Return the appropriate amount of resources used by a {@code Task} back to the global resource
 * list.
 */
void returnResources(Task *task) {
  for (auto &reqResource : task->requiredResources) {
    char resource[MAX_NAME_LEN];
    strcpy(resource, reqResource.c_str());
    char* resName = strtok(resource, ":");
    resources[resName] += strToInt(strtok(nullptr, ":"));
  }
}

/**
 * After a {@code Task} is created it needs to run its specified amount of iterations. This provides
 * controller logic as to make sure other mutexes don't encounter race conditions with other
 * threads. While allowing the {@code Task} to run until its specified number of iterations is met.
 */
void runIterations(Task *task) {
  int iterationCounter = 0;
  clock_t waitStart, waitFinish; //used to determine how long a task will wait
  struct tms tmswaitstart{}, tmswaitend{};

  lockMutex(&monitorMutex); //make sure cannot change state if monitor is printing
  task->status = WAIT;
  unlockMutex(&monitorMutex);

  waitStart = times(&tmswaitstart);
  while (true) {
    lockMutex(&iterationMutex);

    // check if resources are available to grab, if not then unlock and go back to waiting
    if (!hasEnoughResources(task)) {
      unlockMutex(&iterationMutex);
      delay(100); // Slight delay before continuing
      continue;
    }

    assignResources(task); // Assign resources to task from shared resource pool
    waitFinish = times(&tmswaitend);
    task->totalWaitTime += ((waitFinish - waitStart) / clktck) * 1000;
    unlockMutex(&iterationMutex);

    // after resources are taken, simulate the execution of the process
    lockMutex(&monitorMutex); // cant switch states if monitor is printing
    task->status = RUN;
    unlockMutex(&monitorMutex);
    delay(task->busyTime);
    task->totalBusyTime += task->busyTime;

    // after running the busytime then return the resources back to the pool
    lockMutex(&iterationMutex);
    returnResources(task);
    unlockMutex(&iterationMutex);

    // now we wait for idle time and increment iteration counter
    lockMutex(&monitorMutex); // cant switch states if monitor printing
    task->status = IDLE;
    unlockMutex(&monitorMutex);

    delay(task->idleTime);
    task->totalIdleTime += task->idleTime;
    iterationCounter += 1;
    task->timesExecuted += 1;

    if (iterationCounter == nIter) return;

    lockMutex(&monitorMutex); // cant switch states if monitor printing
    task->status = WAIT;
    unlockMutex(&monitorMutex);
    waitStart = times(&tmswaitstart);
  }
}

/**
 * Starting method for when a new task thread is created.
 * @param arg {@code long}
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
 * Print information about the simulator. (e.g. resource info, task info, total runtime).
 * Should be invoked before termination of the simulator.
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
      char *resourceName;
      char resourceString[MAX_NAME_LEN];
      strcpy(resourceString, reqResource.c_str());
      resourceName = strtok(resourceString, ":");

      printf("\t %s: (needed=\t%d, held= 0)\n", resourceName, strToInt(strtok(nullptr, ":")));
    }

    printf("\t (RUN: %d times, WAIT: %lu msec\n\n", taskList.at(i).timesExecuted,
           taskList.at(i).totalWaitTime);
  }
}

int main(int argc, char **argv) {
  if (argc != 4) {
    printf("Error: Invalid arguments. Expected 'a4tasks inputFile monitorTime nIter'\n");
    exit(EXIT_FAILURE);
  }

  string inputFile = argv[1];
  long monitorTime = strtol(argv[2], (char **) nullptr, 10);
  if (errno) {
    perror("Invalid monitorTime: strtol() failure");
    return errno;
  }

  nIter = (uint) strtol(argv[3], (char **) nullptr, 10);
  if (errno) {
    perror("Invalid nIter: strtol() failure");
    return errno;
  }

  char fileName[MAX_FILENAME_LEN];
  strcpy(fileName, inputFile.c_str());

  initMutex(&threadMutex);
  initMutex(&iterationMutex);
  initMutex(&monitorMutex);

  // Get number of clock cycles per second. Used for timing functions
  if (clktck == 0) {
    if ((clktck = sysconf(_SC_CLK_TCK)) < 0) {
      perror("sysconf() failure");
      return errno;
    }
  }

  parseTaskFile(fileName);

  pthread_t newPthreadId;

  // Create a monitor pthread
  pthread_create(&newPthreadId, nullptr, monitorThread, (void *) monitorTime);
  if (errno) {
    perror("Monitor pthread creation error: pthread_create() failure");
    exit(errno);
  }

  // Create a new pthread for every task in the task list
  for (long i = 0; i < taskList.size(); i++) {
    lockMutex(&threadMutex);
    pthread_create(&newPthreadId, nullptr, executeThread, (void *) i);
    if (errno) {
      perror("Task list pthread creation error: pthread_create() failure");
      exit(errno);
    }
  }

  delay(400);

  // Wait for all other threads to complete before continuing
  for (long i = 0; i < taskList.size(); i++) {
    pthread_join(taskThreadList[i], nullptr);
    if (errno) {
      perror("pthread_join() failure");
      exit(errno);
    }
  }

  printTerminationInfo();

  return EXIT_SUCCESS;
}