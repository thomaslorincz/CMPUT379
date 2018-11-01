#ifndef UTIL_H_
#define UTIL_H_

#include <string>
#include <vector>

using namespace std;

typedef struct {
  string type;
  string message;
} Packet;

vector<int> ParsePacketMessage(string &m);

string MakeFifoName(int sender_id, int receiver_id);

string PacketToString(Packet &p);

Packet ParsePacketString(string &s);

int ParseSwitchId(const string &input);

void Trim(string &s);

#endif