#include <fcntl.h>
#include <sys/resource.h>
#include <fstream>
#include <sstream>
#include <string>
#include <tuple>
#include "controller.h"
#include "switch.h"

#define MAX_NSW 7
#define MAXIP 1000

using namespace std;

// TODO: Perhaps error handling
int ParseSwitchId(const string &input) {
  if (input == "null") {
    return -1;
  } else {
    string number = input.substr(2, input.length() - 1);
    return (int)strtol(number.c_str(), (char **)NULL, 10);
  }
}

tuple<int, int> ParseIpRange(const string &input) {
  int ip_low;
  int ip_high;

  stringstream ss(input);
  string token;

  int i = 0;
  while (getline(ss, token, '-')) {
    if (token.length()) {
      if (i == 0) {
        ip_low = (int)strtol(token.c_str(), (char **)NULL, 10);
      } else if (i == 1) {
        ip_high = (int)strtol(token.c_str(), (char **)NULL, 10);
      }
      i++;
    } else {
      return make_tuple(-1, -1);
    }
  }

  return make_tuple(ip_low, ip_high);
}

/**
 * Main function. Processes command line arguments into inputs for either the
 * controller loop or the switch loop.
 */
int main(int argc, char **argv) {
  // Set a 10 minute CPU time limit
  rlimit time_limit{.rlim_cur = 600, .rlim_max = 600};
  setrlimit(RLIMIT_CPU, &time_limit);

  if (argc < 2) {
    printf("Too few arguments.\n");
    return 1;
  }

  string mode = argv[1];  // cont or swi
  if (mode == "cont") {
    if (argc != 3) {
      printf("Error: Invalid number of arguments. Expected 3.\n");
      return 1;
    }

    int num_switches = (int)strtol(argv[2], (char **)NULL, 10);
    if (num_switches > MAX_NSW || num_switches < 1) {
      printf("Error: Invalid number of switches. Must be 1-7.\n");
      return 1;
    }

    ControllerLoop(num_switches);
  } else if (mode.find("sw") != std::string::npos) {
    if (argc != 6) {
      printf("Error: Invalid number of arguments. Expected 6.\n");
      return 1;
    }

    int switch_id = ParseSwitchId(argv[1]);
    if (switch_id == -1) {
      printf("Error: Switch ID is invalid.\n");
      return 1;
    }

    ifstream in(argv[2]);

    if (!in) {
      printf("Error: Cannot open file.\n");
      return 1;
    }

    int switch_id_1 = ParseSwitchId(argv[3]);
    int switch_id_2 = ParseSwitchId(argv[4]);

    tuple<int, int> ip_range = ParseIpRange(argv[5]);

    if (get<0>(ip_range) == -1 && get<1>(ip_range) == -1) {
      printf("Error: Malformed IP range.\n");
      return 1;
    }

    SwitchLoop(switch_id, switch_id_1, switch_id_2, ip_range, in);
  }

  return 0;
}
