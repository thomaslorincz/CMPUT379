#ifndef UTIL_H_
#define UTIL_H_

#include <string>
#include <utility>
#include <vector>

using namespace std;

string MakeFifoName(int sender_id, int receiver_id);

pair<string, vector<int>> ParsePacketString(string &s);

int ParseSwitchId(const string &input);

void Trim(string &s);

#endif