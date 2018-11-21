#ifndef UTIL_H_
#define UTIL_H_

#include <string>
#include <utility>
#include <vector>

using namespace std;

string makeFifoName(int senderId, int receiverId);

pair<string, vector<int>> parsePacketString(string &s);

int parseSwitchId(const string &input);

void trim(string &s);

void sendOpenPacket(int fd, int id, int port1Id, int port2Id, int ipLow, int ipHigh);

void sendAckPacket(int fd);

void sendAddPacket(int fd, int action, int ipLow, int ipHigh, int relayId);

void sendQueryPacket(int fd, int destIp);

void sendRelayPacket(int fd, int destIp);

#endif