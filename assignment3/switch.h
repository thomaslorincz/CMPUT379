#ifndef SWITCH_H_
#define SWITCH_H_

#include <fstream>
#include <tuple>

using namespace std;

void switchLoop(int id, int port1Id, int port2Id, int ipLow, int ipHigh, ifstream &in,
                string &ipAddress, uint16_t portNumber);

#endif