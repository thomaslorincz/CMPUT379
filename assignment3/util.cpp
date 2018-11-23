#include <algorithm>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include <unistd.h>
#include <cstring>
#include <poll.h>

using namespace std;

/**
 * Parses the message portion of a packet
 * Attribution:
 * https://stackoverflow.com/questions/1894886/parsing-a-comma-delimited-stdstring
 * https://stackoverflow.com/a/1894955
 */
vector<int> parsePacketMessage(string &message) {
  vector<int> packetContents;
  stringstream ss(message);

  // Split packet string into ints (comma delimited)
  int i = 0;
  while (ss >> i) {
    packetContents.push_back(i);
    if (ss.peek() == ',') ss.ignore();
  }

  return packetContents;
}

/**
 * Returns FIFO name based on sender and receiver IDs
 */
string makeFifoName(int senderId, int receiverId) {
  return "fifo-" + to_string(senderId) + "-" + to_string(receiverId);
}

/**
 * Parse a packet string. Return the packet type and its message info.
 */
pair<string, vector<int>> parsePacketString(string &s) {
  string packetType = s.substr(0, s.find(':'));

  string packetMessageToken = s.substr(s.find(':') + 1);
  vector<int> packetMessage = parsePacketMessage(packetMessageToken);

  return make_pair(packetType, packetMessage);
}

/**
 * Parses switch ID from command line argument input.
 * Returns switch ID if ID is valid. Returns -1 if switch has no connection to
 * its port. Exits program if the switch ID is invalid.
 */
int parseSwitchId(const string &input) {
  if (input == "null") {
    return -1;
  } else {
    string number = input.substr(2, input.length() - 1);
    int switchId = (int) strtol(number.c_str(), (char **) nullptr, 10);
    if (switchId < 1 || switchId > 7 || errno) {
      printf("Error: Invalid switch ID %i. Expected 1-7.\n", switchId);
      exit(EXIT_FAILURE);
    }
    return switchId;
  }
}

/**
 * Left and right trim the string (in place)
 * String trimming function found here:
 * https://stackoverflow.com/questions/216823/whats-the-best-way-to-trim-stdstring
 * https://stackoverflow.com/a/217605 
 * By: https://stackoverflow.com/users/13430/evan-teran
 */
void trim(string &s) {
  s.erase(s.begin(), find_if(s.begin(), s.end(), [](int ch) { return !isspace(ch); }));
  s.erase(find_if(s.rbegin(), s.rend(), [](int ch) { return !isspace(ch); }).base(), s.end());
}

/**
 * Print a formatted message based on a transmitted/received packet.
 */
void printPacketMessage(string &direction, int srcId, int destId, string &type, vector<int> msg) {
  string src = "sw" + to_string(srcId);
  string dest = "sw" + to_string(destId);

  string packetString;
  if (type == "OPEN") {
    dest = "cont";

    string port1;
    if (msg[1] == -1) {
      port1 = "null";
    } else {
      port1 = "sw" + to_string(msg[1]);
    }

    string port2;
    if (msg[2] == -1) {
      port2 = "null";
    } else {
      port2 = "sw" + to_string(msg[2]);
    }

    packetString = ":\n         (port0= cont, port1= "+ port1 + ", port2= " + port2 + ", port3= " +
                   to_string(msg[3]) + "-" + to_string(msg[4]) + ")";
  } else if (type == "ACK") {
    src = "cont";
    packetString = "";
  } else if (type == "QUERY") {
    dest = "cont";

    packetString = ":  header= (srcIP= " + to_string(msg[0]) + ", destIP= " + to_string(msg[1]) +
                   ")";
  } else if (type == "ADD") {
    src = "cont";

    string action;
    if (msg[0] == 0) {
      action = "DROP";
    } else if (msg[0] == 1) {
      action = "FORWARD";
    }

    packetString = ":\n         (srcIp= 0-1000, destIp= " + to_string(msg[1]) + "-" +
                   to_string(msg[2]) + ", action= " + action + ":" + to_string(msg[3]) +
                   ", pri= 4, pktCount= 0";
  } else if (type == "RELAY") {
    packetString = ":  header= (srcIP= " + to_string(msg[0]) +", destIP= " +
                   to_string(msg[1]) + ")";
  }

  printf("%s (src= %s, dest= %s) [%s]%s\n", direction.c_str(), src.c_str(),
         dest.c_str(), type.c_str(), packetString.c_str());
}