#include <fcntl.h>
#include <sys/resource.h>
#include <fstream>
#include <sstream>
#include <string>
#include <tuple>
#include "controller.h"
#include "switch.h"
#include "util.h"

#define MAX_NSW 7
#define MAXIP 1000

using namespace std;

/**
 * Parses IP range from command line argument input.
 * Returns a tuple comprised of the lower and upper IP range bounds if
 * successful. Exits the program if there is an error in parsing.
 */
tuple<int, int> parseIpRange(const string &input) {
  int ipLow = 0;
  int ipHigh = 0;

  stringstream ss(input);
  string token;

  int i = 0;
  while (getline(ss, token, '-')) {
    if (token.length()) {
      if (i == 0) {
        ipLow = (int) strtol(token.c_str(), (char**) nullptr, 10);
        if (ipLow < 0 || ipLow > MAXIP || errno) {
          printf("Error: Invalid IP lower bound.\n");
          exit(1);
        }
      } else if (i == 1) {
        ipHigh = (int)strtol(token.c_str(), (char**) nullptr, 10);
        if (ipHigh < 0 || ipHigh > MAXIP || errno) {
          printf("Error: Invalid IP lower bound.\n");
          exit(1);
        }
      }
      i++;
    } else {
      printf("Error: Malformed IP range.\n");
      exit(1);
    }
  }

  if (ipHigh < ipLow) {
    printf("Error: Invalid range.\n");
    exit(1);
  }

  return make_tuple(ipLow, ipHigh);
}

/**
 * Main function. Processes command line arguments into inputs for either the
 * controller loop or the switch loop.
 */
int main(int argc, char **argv) {
  // Set a 10 minute CPU time limit
  rlimit timeLimit{.rlim_cur = 600, .rlim_max = 600};
  setrlimit(RLIMIT_CPU, &timeLimit);

  if (argc < 2) {
    printf("Too few arguments.\n");
    return 1;
  }

  string mode = argv[1];  // cont or swi
  if (mode == "cont") {
    if (argc != 4) {
      printf("Error: Invalid number of arguments. Expected 4.\n");
      return 1;
    }

    int numSwitches = (int) strtol(argv[2], (char **) nullptr, 10);
    if (numSwitches > MAX_NSW || numSwitches < 1 || errno) {
      printf("Error: Invalid number of switches. Must be 1-7.\n");
      return 1;
    }

    auto portNumber = (uint16_t) strtol(argv[3], (char **) nullptr, 10);
    // TODO: Error checking?

    controllerLoop(numSwitches, portNumber);
  } else if (mode.find("sw") != std::string::npos) {
    if (argc != 8) {
      printf("Error: Invalid number of arguments. Expected 8.\n");
      return 1;
    }

    int switchId = parseSwitchId(argv[1]);

    ifstream in(argv[2]);

    if (!in) {
      printf("Error: Cannot open file.\n");
      return 1;
    }

    int switchId1 = parseSwitchId(argv[3]);
    int switchId2 = parseSwitchId(argv[4]);

    tuple<int, int> ipRange = parseIpRange(argv[5]);

    string serverAddress = argv[6];
    // TODO: Error checking?

    auto portNumber = (uint16_t) strtol(argv[7], (char **) nullptr, 10);
    // TODO: Error checking?

    switchLoop(switchId, switchId1, switchId2, ipRange, in, serverAddress, portNumber);
  } else {
    printf("Error: Invalid mode specified.\n");
    return 1;
  }

  return 0;
}
