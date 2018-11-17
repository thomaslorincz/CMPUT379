#include <algorithm>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

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