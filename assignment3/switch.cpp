#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <vector>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "util.h"

#define PFDS_SIZE 5
#define CONTROLLER_PORT 0
#define CONTROLLER_ID 0
#define MAX_IP 1000
#define MIN_PRI 4
#define MAX_BUFFER 1024

using namespace std;

typedef struct {
    int admit;
    int ack;
    int add;
    int relayIn;
    int open;
    int query;
    int relayOut;
} SwitchPacketCounts;

typedef struct {
    int srcIpLow;
    int srcIpHigh;
    int destIpLow;
    int destIpHigh;
    string actionType;  // FORWARD, DROP
    int actionVal;
    int pri;  // 0, 1, 2, 3, 4 (highest - lowest)
    int pktCount;
} FlowRule;

void sendOpenPacket(int fd, int id, int port1Id, int port2Id, int ipLow, int ipHigh) {
  string openString = "OPEN:" + to_string(id) + "," + to_string(port1Id) + "," + to_string(port2Id)
                      + "," + to_string(ipLow) + "," + to_string(ipHigh);
  write(fd, openString.c_str(), strlen(openString.c_str()));
  if (errno) {
    perror("write() failure");
    exit(errno);
  }
}

// TODO: srcIp
void sendQueryPacket(int fd, int destIp) {
  string queryString = "QUERY:" + to_string(destIp);
  write(fd, queryString.c_str(), strlen(queryString.c_str()));
  if (errno) {
    perror("write() failure");
    exit(errno);
  }
}

void sendRelayPacket(int fd, int destIp) {
  string relayString = "RELAY:" + to_string(destIp);
  write(fd, relayString.c_str(), strlen(relayString.c_str()));
  if (errno) {
    perror("write() failure");
    exit(errno);
  }
}

/**
 * Opens a FIFO for reading or writing.
 */
int openFifo(string &fifo_name, int flag) {
  // Returns lowest unused file descriptor on success
  int fd = open(fifo_name.c_str(), flag);
  if (errno) {
    perror("Failed to open FIFO");
    exit(errno);
  }

  return fd;
}

/**
 * Creates and opens a FIFO for reading or writing.
 */
int createFifo(int src, int dest, int flag) {
  string fifoName = makeFifoName(src, dest);

  mkfifo(fifoName.c_str(), S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
  if (errno) {
    perror("Failed to create FIFO connection");
    exit(errno);
  }

  int fd = openFifo(fifoName, flag);

  printf("Created %s fd = %i\n", fifoName.c_str(), fd);

  return fd;
}

/**
 * Parses a line in the traffic file.
 * Attribution:
 * https://stackoverflow.com/questions/236129/how-do-i-iterate-over-the-words-of-a-string
 * https://stackoverflow.com/a/237280
 */
tuple<int, int, int> parseTrafficFileLine(string &line) {
  int id = 0;
  int srcIp = 0;
  int destIp = 0;

  istringstream iss(line);
  vector<string> tokens{istream_iterator<string>{iss}, istream_iterator<string>{}};

  if (tokens[0] == "#") { // TODO: Check first character of line
    return make_tuple(-1, -1, -1);
  } else {
    id = parseSwitchId(tokens[0]);

    if (tokens[1] == "delay") {
      int ms = (int) strtol(tokens[2].c_str(), (char **) nullptr, 10);
      if (ms < 0 || errno) {
        printf("Error: Invalid delay. Skipping line.\n");
        errno = 0;
        return make_tuple(-1, -1, -1);
      }
    } else {
      srcIp = (int) strtol(tokens[1].c_str(), (char **) nullptr, 10);
      if (srcIp < 0 || srcIp > MAX_IP || errno) {
        printf("Error: Invalid IP lower bound.\n");
        errno = 0;
        return make_tuple(-1, -1, -1);
      }

      destIp = (int) strtol(tokens[2].c_str(), (char **) nullptr, 10);
      if (destIp < 0 || destIp > MAX_IP || errno) {
        printf("Error: Invalid IP lower bound.\n");
        errno = 0;
        return make_tuple(-1, -1, -1);
      }
    }
  }

  return make_tuple(id, srcIp, destIp);
}

/**
 * List the status information of the switch.
 */
// TODO: Need to list contents of all seen packets
void switchList(vector<FlowRule> &flowTable, SwitchPacketCounts &counts) {
  printf("Flow table:\n");
  int i = 0;
  for (auto &rule : flowTable) {
    printf("[%i] (srcIp= %i-%i, destIp= %i-%i, ", i, rule.srcIpLow,
           rule.srcIpHigh, rule.destIpLow, rule.destIpHigh);
    printf("action= %s:%i, pri= %i, pktCount= %i)\n", rule.actionType.c_str(),
           rule.actionVal, rule.pri, rule.pktCount);
    i++;
  }
  printf("\n");
  printf("Packet Stats:\n");
  printf("\tReceived:    ADMIT:%i, ACK:%i, ADDRULE:%i, RELAYIN:%i\n", counts.admit, counts.ack,
         counts.add, counts.relayIn);
  printf("\tTransmitted: OPEN:%i, QUERY:%i, RELAYOUT:%i\n", counts.open, counts.query,
         counts.relayOut);
}

/**
 * Main event loop for the switch. Polls all input FIFOS.
 * Sends and receives packets of varying types.
 */
void switchLoop(int id, int port1Id, int port2Id, int ipLow, int ipHigh, ifstream &in,
                string &ipAdress, uint16_t portNumber) {
  vector<FlowRule> flowTable; // Flow rule table
  flowTable.push_back({0, MAX_IP, ipLow, ipHigh, "FORWARD", 3, MIN_PRI, 0}); // Add initial rule

  map<int, int> portToFd; // Map port number to FD
  map<int, int> portToId; // Map port number to switch ID

  // Counts the number of each type of packet seen
  SwitchPacketCounts counts = {0, 0, 0, 0, 0, 0, 0};

  int socketIdx = PFDS_SIZE - 1;

  char buffer[MAX_BUFFER];
  struct pollfd pfds[PFDS_SIZE];

  // Set up STDIN for polling from
  pfds[0].fd = STDIN_FILENO;
  pfds[0].events = POLLIN;
  pfds[0].revents = 0;

  struct sockaddr_in server {};

  // Creating socket file descriptor
  if ((pfds[socketIdx].fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
    perror("socket() failure");
    exit(errno);
  }
  memset(&server, 0, sizeof(server));

  server.sin_family = AF_INET;
  server.sin_port = htons(portNumber);

  // Convert IPv4 and IPv6 addresses from text to binary form
  if (inet_pton(AF_INET, ipAdress.c_str(), &server.sin_addr) <= 0) {
    perror("Invalid IP address");
    exit(errno);
  }

  if (connect(pfds[socketIdx].fd, (struct sockaddr *) &server, sizeof(server)) < 0) {
    perror("connect() failure");
    exit(errno);
  }

  pair<int, int> controllerToFd = make_pair(0, pfds[socketIdx].fd);
  portToFd.insert(controllerToFd);
  pair<int, int> controllerToId = make_pair(0, CONTROLLER_ID);
  portToId.insert(controllerToId);

  // Send an OPEN packet to the controller
  sendOpenPacket(pfds[socketIdx].fd, id, port1Id, port2Id, ipLow, ipHigh);
  counts.open++;

  // Set socket to non-blocking
  if (fcntl(pfds[socketIdx].fd, F_SETFL, fcntl(pfds[socketIdx].fd, F_GETFL) | O_NONBLOCK) < 0) {
    perror("fnctl() failure");
    exit(errno);
  }

  // Create and open a reading FIFO for port 1 if not null
  if (port1Id != -1) {
    pair<int, int> port1Connection = make_pair(1, port1Id);
    portToId.insert(port1Connection);
    int port1Fd = createFifo(port1Id, id, O_RDONLY | O_NONBLOCK);
    pfds[1].fd = port1Fd;
    pfds[1].events = POLLIN;
    pfds[1].revents = 0;
  }

  // Create and open a reading FIFO for port 2 if not null
  if (port2Id != -1) {
    pair<int, int> port2Connection = make_pair(2, port2Id);
    portToId.insert(port2Connection);
    int port2Fd = createFifo(port2Id, id, O_RDONLY | O_NONBLOCK);
    pfds[2].fd = port2Fd;
    pfds[2].events = POLLIN;
    pfds[2].revents = 0;
  }

  bool ackReceived = false, addReceived = true;

  while (true) {
    /*
     * 1. Read and process a single line from the traffic line (if the EOF has not been reached
     * yet). The switch ignores empty lines, comment lines, and lines specifying other handling
     * switches. A packet header is considered admitted if the line specifies the current switch.
     */
    if (ackReceived && addReceived) {
      tuple<int, int, int> trafficInfo;
      string line;
      if (in.is_open()) {
        if (getline(in, line)) {
          trafficInfo = parseTrafficFileLine(line);

          int trafficId = get<0>(trafficInfo);
          int srcIp = get<1>(trafficInfo);
          int destIp = get<2>(trafficInfo);

          if (id != trafficId || (trafficId == -1 && srcIp == -1 && destIp == -1)) {
            // Ignore
          } else {
            counts.admit++;

            // Handle the packet using the flow table
            bool found = false;
            for (auto &rule : flowTable) {
              if (destIp >= rule.destIpLow && destIp <= rule.destIpHigh) {
                rule.pktCount++;
                if (rule.actionType == "DROP") {
                  break;
                } else if (rule.actionType == "FORWARD") {
                  if (rule.actionVal != 3) {
                    // Open the FIFO for writing if not done already
                    if (!portToFd.count(rule.actionVal)) {
                      string relayFifo = makeFifoName(id, portToId[rule.actionVal]);
                      int portFd = openFifo(relayFifo, O_WRONLY | O_NONBLOCK);
                      pair<int, int> portConn = make_pair(rule.actionVal, portFd);
                      portToFd.insert(portConn);
                    }

                    sendRelayPacket(portToFd[rule.actionVal], destIp);
                    counts.relayOut++;
                  }
                }

                found = true;
                break;
              }
            }

            if (!found) {
              sendQueryPacket(portToFd[CONTROLLER_PORT], destIp);
              addReceived = false;
              counts.query++;
            }
          }
        } else {
          in.close();
        }
      }
    }

    if (poll(pfds, (nfds_t) PFDS_SIZE, 0) == -1) { // Poll from all file descriptors
      perror("poll() failure");
      exit(errno);
    }

    /*
     * 2. Poll the keyboard for a user command. The user can issue one of the following commands.
     * list: The program writes all entries in the flow table, and for each transmitted or received
     * packet type, the program writes an aggregate count of handled packets of this type. exit: The
     * program writes the above information and exits.
     */
    if (pfds[0].revents & POLLIN) {
      if (!read(pfds[0].fd, buffer, MAX_BUFFER)) {
        printf("Error: stdin closed.\n");
        exit(EXIT_FAILURE);
      }

      string cmd = string(buffer);
      trim(cmd);  // trim whitespace

      if (cmd == "list") {
        switchList(flowTable, counts);
      } else if (cmd == "exit") {
        switchList(flowTable, counts);
        exit(EXIT_SUCCESS);
      } else {
        printf("Error: Unrecognized command. Please use list or exit.\n");
      }
    }

    memset(buffer, 0, sizeof(buffer)); // Clear buffer

    /*
     * 3. Poll the incoming FIFOs from the controller and the attached switches. The switch handles
     * each incoming packet, as described in the Packet Types section.
     */
    for (int i = 1; i < PFDS_SIZE; i++) {
      if (pfds[i].revents & POLLIN) {
        if (!read(pfds[i].fd, buffer, MAX_BUFFER)) {
          printf("Warning: Connection closed.\n"); // TODO: Connection to where?
          close(pfds[i].fd);
          continue;
        }
        string packetString = string(buffer);
        pair<string, vector<int>> receivedPacket = parsePacketString(packetString);
        string packetType = get<0>(receivedPacket);
        vector<int> packetMessage = get<1>(receivedPacket);

        string direction = "Received";
        printPacketMessage(direction, i, id, packetType, packetMessage);

        if (packetType == "ACK") {
          ackReceived = true;
          counts.ack++;
        } else if (packetType == "ADD") {
          addReceived = true;

          FlowRule newRule;

          if (packetMessage[0] == 0) {
            newRule = {0, MAX_IP, packetMessage[1], packetMessage[2], "DROP", packetMessage[3],
                       MIN_PRI, 1};
          } else if (packetMessage[0] == 1) {
            newRule = {0, MAX_IP, packetMessage[1], packetMessage[2], "FORWARD", packetMessage[3],
                       MIN_PRI, 1};

            // Open FIFO for writing if not done so already
            if (!portToFd.count(packetMessage[3])) {
              string relayFifo = makeFifoName(id, portToId[packetMessage[3]]);
              int portFd = openFifo(relayFifo, O_WRONLY | O_NONBLOCK);
              pair<int, int> portConnection = make_pair(packetMessage[3], portFd);
              portToFd.insert(portConnection);
            }

            sendRelayPacket(portToFd[packetMessage[3]], packetMessage[1]);
            counts.relayOut++;
          } else {
            printf("Error: Invalid rule to add.\n");
            continue;
          }

          flowTable.push_back(newRule);
          counts.add++;
        } else if (packetType == "RELAY") {
          counts.relayIn++;

          // Relay the packet to an adjacent controller if the destIp is not meant for this switch
          if (packetMessage[0] < ipLow || packetMessage[0] > ipHigh) {
            if (id > i) {
              sendRelayPacket(portToFd[1], packetMessage[0]);
              counts.relayOut++;
            } else if (id < i) {
              sendRelayPacket(portToFd[2], packetMessage[0]);
              counts.relayOut++;
            }
          } else {
            // TODO: Print something about message being delivered successfully?
          }
        } else {
          // Unknown packet. Used for debugging.
          printf("Received %s packet. Ignored.\n", packetType.c_str());
        }
      }
    }

    memset(buffer, 0, sizeof(buffer)); // Clear buffer
  }
}