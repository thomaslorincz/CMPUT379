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
 * Returns FIFO name based on sender and reciever IDs
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
      exit(1);
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

void sendOpenPacket(int fd, int id, int port1Id, int port2Id, int ipLow, int ipHigh) {
  string openString = "OPEN:" + to_string(id) + "," + to_string(port1Id) + "," + to_string(port2Id)
      + "," + to_string(ipLow) + "," + to_string(ipHigh);
  write(fd, openString.c_str(), strlen(openString.c_str()));
  if (errno) {
    perror("Error: Failed to write.");
    exit(errno);
  }

}

void sendAckPacket(int fd) {
  string ackString = "ACK:";
  write(fd, ackString.c_str(), strlen(ackString.c_str()));
  if (errno) {
    perror("Error: Failed to write");
  }
}

void sendAddPacket(int fd, int action, int ipLow, int ipHigh, int relayId) {
  string addString = "ADD:" + to_string(action) + "," + to_string(ipLow) + "," + to_string(ipHigh)
      + "," + to_string(relayId);
  write(fd, addString.c_str(), strlen(addString.c_str()));
  if (errno) {
    perror("Error: Failed to write.");
  }
}

void sendQueryPacket(int fd, int destIp) {
  string queryString = "QUERY:" + to_string(destIp);
  write(fd, queryString.c_str(), strlen(queryString.c_str()));
  if (errno) {
    perror("Error: Failed to write.");
    exit(errno);
  }
}

void sendRelayPacket(int fd, int destIp) {
  string relayString = "RELAY:" + to_string(destIp);
  write(fd, relayString.c_str(), strlen(relayString.c_str()));
  if (errno) {
    perror("Error: Failed to write.");
    exit(errno);
  }
}