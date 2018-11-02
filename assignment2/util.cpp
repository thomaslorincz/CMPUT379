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
vector<int> ParsePacketMessage(string &m) {
  vector<int> vect;
  stringstream ss(m);

  // Split packet string into ints (comma delimited)
  int i = 0;
  while (ss >> i) {
    vect.push_back(i);
    if (ss.peek() == ',') ss.ignore();
  }

  return vect;
}

/**
 * Returns FIFO name based on sender and reciever IDs
 */
string MakeFifoName(int sender_id, int receiver_id) {
  return "fifo-" + to_string(sender_id) + "-" + to_string(receiver_id);
}

/**
 * Parse a packet string. Return the packet type and its message info.
 */
pair<string, vector<int>> ParsePacketString(string &s) {
  string packet_type = s.substr(0, s.find(":"));

  string packet_message_token = s.substr(s.find(":") + 1);
  vector<int> packet_message = ParsePacketMessage(packet_message_token);

  return make_pair(packet_type, packet_message);
}

/**
 * Parses switch ID from command line argument input.
 * Returns switch ID if ID is valid. Returns -1 if switch has no connection to
 * its port. Exits program if the switch ID is invalid.
 */
int ParseSwitchId(const string &input) {
  if (input == "null") {
    return -1;
  } else {
    string number = input.substr(2, input.length() - 1);
    int switch_id = (int)strtol(number.c_str(), (char **)NULL, 10);
    if (switch_id < 1 || switch_id > 7 || errno) {
      printf("Error: Invalid switch ID %i. Expected 1-7.\n", switch_id);
      exit(1);
    }
    return switch_id;
  }
}

/**
 * String trimming functions found here:
 * https://stackoverflow.com/questions/216823/whats-the-best-way-to-trim-stdstring
 * https://stackoverflow.com/a/217605 
 * By: https://stackoverflow.com/users/13430/evan-teran
 */

// Trim from start (in place)
void LeftTrim(string &s) {
  s.erase(s.begin(),
          find_if(s.begin(), s.end(), [](int ch) { return !isspace(ch); }));
}

// Trim from end (in place)
void RightTrim(string &s) {
  s.erase(
      find_if(s.rbegin(), s.rend(), [](int ch) { return !isspace(ch); }).base(),
      s.end());
}

// Trim from both ends (in place)
void Trim(string &s) {
  LeftTrim(s);
  RightTrim(s);
}