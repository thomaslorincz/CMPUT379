#ifndef SWITCH_H_
#define SWITCH_H_

#include <fstream>
#include <tuple>

using namespace std;

void SwitchLoop(int id, int port_1_id, int port_2_id, tuple<int, int> ip_range,
                ifstream &in);

#endif