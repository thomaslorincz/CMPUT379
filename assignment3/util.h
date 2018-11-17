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

#endif