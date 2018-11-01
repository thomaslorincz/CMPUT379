#ifndef UTIL_H_
#define UTIL_H_

#include <string>

using namespace std;

typedef enum { OPEN, ACK, QUERY, ADD, RELAY } PACKET_TYPE;

typedef struct {
  PACKET_TYPE type;
  string message;
} Packet;

string MakeFifoName(int sender_id, int receiver_id);

string PacketToString(Packet &p);

Packet ParsePacketString(string &s);

int ParseSwitchId(const string &input);

void Trim(string &s);

#endif