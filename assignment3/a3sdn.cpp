#include <fcntl.h>
#include <sys/resource.h>
#include <fstream>
#include <sstream>
#include <string>
#include <tuple>
#include <netdb.h>
#include <cstring>
#include <arpa/inet.h>
#include "controller.h"
#include "switch.h"
#include "util.h"

#define MAX_NSW 7
#define MAX_IP 1000

using namespace std;

/**
 * Function used to get the IP address from a specified IP or symbolic name.
 * Attribution:
 * http://www.logix.cz/michal/devel/various/getaddrinfo.c.xp
 */
string getAddressInfo(string &address) {
  struct addrinfo hints {}, *res;
  char ipAddress[100];
  void *ptr;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = PF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags |= AI_CANONNAME;

  int errorCode = getaddrinfo(address.c_str(), nullptr, &hints, &res);
  if (errorCode != 0) {
    perror("getaddrinfo() failure");
    exit(errorCode);
  }

  printf("Host: %s\n", address.c_str());
  while (res) {
    inet_ntop(res->ai_family, res->ai_addr->sa_data, ipAddress, 100);

    switch (res->ai_family) {
      case AF_INET:
        ptr = &((struct sockaddr_in *) res->ai_addr)->sin_addr;
        break;
      case AF_INET6:
        ptr = &((struct sockaddr_in6 *) res->ai_addr)->sin6_addr;
        break;
      default:
        printf("Error: Invalid IP family. Expected AF_NET or AF_NET6.\n");
        exit(EXIT_FAILURE);
    }
    inet_ntop(res->ai_family, ptr, ipAddress, 100);
    printf("IPv%d address: %s (%s)\n", res->ai_family == PF_INET6 ? 6 : 4,
           ipAddress, res->ai_canonname);
    res = res->ai_next;
  }

  return ipAddress;
}

/**
 * Parses IP range from command line argument input. Returns a tuple comprised of the lower and
 * upper IP range bounds if successful. Exits the program if there is an error in parsing.
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
        ipLow = (int) strtol(token.c_str(), (char **) nullptr, 10);
        if (ipLow < 0 || ipLow > MAX_IP || errno) {
          printf("Error: Invalid IP lower bound.\n");
          exit(EXIT_FAILURE);
        }
      } else if (i == 1) {
        ipHigh = (int) strtol(token.c_str(), (char **) nullptr, 10);
        if (ipHigh < 0 || ipHigh > MAX_IP || errno) {
          printf("Error: Invalid IP lower bound.\n");
          exit(EXIT_FAILURE);
        }
      }
      i++;
    } else {
      printf("Error: Malformed IP range.\n");
      exit(EXIT_FAILURE);
    }
  }

  if (ipHigh < ipLow) {
    printf("Error: Invalid range.\n");
    exit(EXIT_FAILURE);
  }

  return make_tuple(ipLow, ipHigh);
}

/**
 * Main function. Processes command line arguments into inputs for either the controller loop or the
 * switch loop.
 */
int main(int argc, char **argv) {
  // Set a 10 minute CPU time limit
  rlimit timeLimit{.rlim_cur = 600, .rlim_max = 600};
  setrlimit(RLIMIT_CPU, &timeLimit);

  if (argc < 2) {
    printf("Too few arguments.\n");
    return EXIT_FAILURE;
  }

  string mode = argv[1];  // cont or swi
  if (mode == "cont") {
    if (argc != 4) {
      printf("Error: Invalid number of arguments. Expected 4.\n");
      return EXIT_FAILURE;
    }

    int numSwitches = (int) strtol(argv[2], (char **) nullptr, 10);
    if (numSwitches > MAX_NSW || numSwitches < 1 || errno) {
      printf("Error: Invalid number of switches. Must be 1-7.\n");
      return EXIT_FAILURE;
    }

    auto portNumber = (uint16_t) strtol(argv[3], (char **) nullptr, 10);

    controllerLoop(numSwitches, portNumber);
  } else if (mode.find("sw") != std::string::npos) {
    if (argc != 8) {
      printf("Error: Invalid number of arguments. Expected 8.\n");
      return EXIT_FAILURE;
    }

    int switchId = parseSwitchId(argv[1]);

    ifstream in(argv[2]);

    if (!in) {
      printf("Error: Cannot open file.\n");
      return EXIT_FAILURE;
    }

    int switchId1 = parseSwitchId(argv[3]);
    int switchId2 = parseSwitchId(argv[4]);

    tuple<int, int> ipRange = parseIpRange(argv[5]);

    string serverAddress = argv[6];
    string ipAddress = getAddressInfo(serverAddress);
    printf("Found IP: %s\n", ipAddress.c_str());

    auto portNumber = (uint16_t) strtol(argv[7], (char **) nullptr, 10);

    switchLoop(switchId, switchId1, switchId2, get<0>(ipRange), get<1>(ipRange), in, ipAddress,
               portNumber);
  } else {
    printf("Error: Invalid mode specified. Expected cont or swi.\n");
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
