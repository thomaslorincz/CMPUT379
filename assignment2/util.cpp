#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

typedef struct {
  string type;
  string message;
} Packet;

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

string MakeFifoName(int sender_id, int receiver_id) {
  return "fifo-" + to_string(sender_id) + "-" + to_string(receiver_id);
}

string PacketToString(Packet &p) {
  return p.type + ":" + p.message;
}

Packet ParsePacketString(string &s) {
  string packet_type = s.substr(0, s.find(":"));

  string packet_message_token = s.substr(s.find(":") + 1);

  return {packet_type, packet_message_token};
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
      printf("Error: Invalid switch ID. Expected 1-7.\n");
      exit(1);
    }
    return switch_id;
  }
}

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