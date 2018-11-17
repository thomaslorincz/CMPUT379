#ifndef SWITCH_H_
#define SWITCH_H_

#include <fstream>
#include <tuple>

using namespace std;

void switchLoop(int id, int port1Id, int port2Id, tuple<int, int> ipRange, ifstream &in,
                string &serverAddress, uint16_t portNumber);

#endif