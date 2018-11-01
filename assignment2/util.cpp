#include <algorithm>
#include <string>

using namespace std;

typedef enum { OPEN, ACK, QUERY, ADD, RELAY } PACKET_TYPE;

typedef struct {
  PACKET_TYPE type;
  string message;
} Packet;

string MakeFifoName(int sender_id, int receiver_id) {
  return "fifo-" + to_string(sender_id) + "-" + to_string(receiver_id);
}

string PacketToString(Packet &p) {
  return to_string(p.type) + ":" + p.message;
}

Packet ParsePacketString(string &s) {
  string packet_type_token = s.substr(0, s.find(":"));
  PACKET_TYPE type =
      (PACKET_TYPE)strtol(packet_type_token.c_str(), (char **)NULL, 10);

  string packet_message_token = s.substr(s.find(":") + 1);

  return {type, packet_message_token};
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