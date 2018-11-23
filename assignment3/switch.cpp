#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <tuple>
#include <utility>
#include <vector>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "util.h"

#define PFDS_SIZE 5
#define CONTROLLER_ID 0
#define MAX_IP 1000
#define MIN_PRI 4
#define MAX_BUFFER 1024

using namespace std;
using namespace chrono;

/**
 * A struct for storing the switch's packet counts
 */
typedef struct {
    int admit;
    int ack;
    int add;
    int relayIn;
    int open;
    int query;
    int relayOut;
} SwitchPacketCounts;

/**
 * A struct representing a rule in the flow table
 */
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

/**
 * Determines whether the switch is still delayed
 * Attribution:
 * https://stackoverflow.com/a/19555298
 * By: https://stackoverflow.com/users/321937/oz
 */
bool isDelayed(long startTime, int duration) {
  if (duration == 0) {
    return false;
  }

  milliseconds currentTime = duration_cast<milliseconds>(system_clock::now().time_since_epoch());
  return currentTime.count() < (startTime + duration);
}

/**
 * Sends an OPEN packet to the controller.
 */
void sendOpenPacket(int fd, int id, int port1Id, int port2Id, int ipLow, int ipHigh) {
  string openString = "OPEN:" + to_string(id) + "," + to_string(port1Id) + "," + to_string(port2Id)
                      + "," + to_string(ipLow) + "," + to_string(ipHigh);
  write(fd, openString.c_str(), strlen(openString.c_str()));
  if (errno) {
    perror("write() failure");
    exit(errno);
  }

  // Log the successful transmission
  string direction = "Transmitted";
  string type = "OPEN";
  pair<string, vector<int>> parsedPacket = parsePacketString(openString);
  printPacketMessage(direction, id, 0, type, parsedPacket.second);
}

/**
 * Send a QUERY packet to the controller.
 */
void sendQueryPacket(int fd, int srcId, int destId, int srcIp, int destIp) {
  string queryString = "QUERY:" + to_string(srcIp) + "," + to_string(destIp);
  write(fd, queryString.c_str(), strlen(queryString.c_str()));
  if (errno) {
    perror("write() failure");
    exit(errno);
  }

  // Log the successful transmission
  string direction = "Transmitted";
  string type = "QUERY";
  pair<string, vector<int>> parsedPacket = parsePacketString(queryString);
  printPacketMessage(direction, srcId, destId, type, parsedPacket.second);
}

/**
 * Send a relay packet to another switch.
 */
void sendRelayPacket(int fd, int srcId, int destId, int srcIp, int destIp) {
  string relayString = "RELAY:" + to_string(srcIp) + "," + to_string(destIp);
  write(fd, relayString.c_str(), strlen(relayString.c_str()));
  if (errno) {
    perror("write() failure");
    exit(errno);
  }

  // Log the successful transmission
  string direction = "Transmitted";
  string type = "RELAY";
  pair<string, vector<int>> parsedPacket = parsePacketString(relayString);
  printPacketMessage(direction, srcId, destId, type, parsedPacket.second);
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
    perror("mkfifo() failure");
    exit(errno);
  }

  return openFifo(fifoName, flag);
}

/**
 * Parses a line in the traffic file.
 * Attribution:
 * https://stackoverflow.com/a/237280
 * By: https://stackoverflow.com/users/30767/zunino
 */
pair<string, vector<int>> parseTrafficFileLine(string &line) {
  string type;
  vector<int> content;

  int id = 0;
  int srcIp = 0;
  int destIp = 0;

  istringstream iss(line);
  vector<string> tokens{istream_iterator<string>{iss}, istream_iterator<string>{}};

  if (line.length() < 1) {
    type = "empty";
  } else if (line.substr(0, 1) == "#") {
    type = "comment";
  } else {
    id = parseSwitchId(tokens[0]);
    content.push_back(id);

    if (tokens[1] == "delay") {
      type = "delay";
      int ms = (int) strtol(tokens[2].c_str(), (char **) nullptr, 10);
      if (ms < 0 || errno) {
        type = "error";
        printf("Error: Invalid delay. Skipping line.\n");
        errno = 0;
      } else {
        content.push_back(ms);
      }
    } else {
      type = "action";

      srcIp = (int) strtol(tokens[1].c_str(), (char **) nullptr, 10);
      if (srcIp < 0 || srcIp > MAX_IP || errno) {
        type = "error";
        printf("Error: Invalid IP lower bound.\n");
        errno = 0;
      } else {
        content.push_back(srcIp);
      }

      destIp = (int) strtol(tokens[2].c_str(), (char **) nullptr, 10);
      if (destIp < 0 || destIp > MAX_IP || errno) {
        type = "error";
        printf("Error: Invalid IP lower bound.\n");
        errno = 0;
      } else {
        content.push_back(destIp);
      }
    }
  }

  return make_pair(type, content);
}

/**
 * List the status information of the switch.
 */
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
 * Main event loop for the switch. Polls all FDs. Sends and receives packets of varying types to
 * communicate within the SDN.
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

  // Used to keep track of the delay interval of the switch
  long delayStartTime = 0;
  int delayDuration = 0;

  bool ackReceived = false, addReceived = true; // Used to wait for controller responses.

  vector<int> closed; // Keep track of which ports are closed

  while (true) {
    /*
     * 1. Read and process a single line from the traffic line (if the EOF has not been reached
     * yet). The switch ignores empty lines, comment lines, and lines specifying other handling
     * switches. A packet header is considered admitted if the line specifies the current switch.
     */
    if (ackReceived && addReceived && !isDelayed(delayStartTime, delayDuration)) {
      // Reset delay variables
      delayStartTime = 0;
      delayDuration = 0;

      pair<string, vector<int>> trafficInfo;
      string line;
      if (in.is_open()) {
        if (getline(in, line)) {
          trafficInfo = parseTrafficFileLine(line);

          string type = trafficInfo.first;
          vector<int> content = trafficInfo.second;

          if (type == "action") {
            int trafficId = content[0];
            int srcIp = content[1];
            int destIp = content[2];

            if (id == trafficId) {
              counts.admit++;

              // Handle the packet using the flow table
              bool found = false;
              for (auto &rule : flowTable) {
                if (destIp >= rule.destIpLow && destIp <= rule.destIpHigh) {
                  found = true;
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

                      // Ensure switch is not closed before sending
                      if (find(closed.begin(), closed.end(), rule.actionVal) == closed.end()) {
                        sendRelayPacket(portToFd[rule.actionVal], id, portToId[rule.actionVal],
                                        srcIp, destIp);
                      }

                      counts.relayOut++;
                    }
                  }

                  break;
                }
              }

              if (!found) {
                sendQueryPacket(portToFd[0], id, 0, srcIp, destIp);
                addReceived = false;
                counts.query++;
              }
            }
          } else if (type == "delay") {
            int trafficId = content[0];
            if (id == trafficId) {
            /*
             * Attribution:
             * https://stackoverflow.com/a/19555298
             * By: https://stackoverflow.com/users/321937/oz
             */
              milliseconds ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch());
              delayStartTime = ms.count();
              delayDuration = content[1];
              printf("Entering a delay period of %i milliseconds.\n", delayDuration);
            }
          } else {
            // Ignore comments, empty lines, or errors.
          }
        } else {
          in.close();
        }
      }
    }

    // Poll from all file descriptors
    if (poll(pfds, (nfds_t) PFDS_SIZE, 0) == -1) {
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
        printf("Error: Unrecognized command. Please use \"list\" or \"exit\".\n");
      }
    }

    memset(buffer, 0, sizeof(buffer)); // Clear buffer

    /*
     * 3. Poll the incoming FDs from the controller and the attached switches. The switch handles
     * each incoming packet, as described in the Packet Types section.
     */
    for (int i = 1; i < PFDS_SIZE; i++) {
      if (pfds[i].revents & POLLIN) {
        if (!read(pfds[i].fd, buffer, MAX_BUFFER)) {
          if (i == socketIdx) {
            printf("Controller closed. Exiting.\n");
            switchList(flowTable, counts);
            exit(errno);
          } else {
            printf("Warning: Connection to sw%i closed.\n", portToId[i]);
            close(pfds[i].fd);
            closed.push_back(i);
            continue;
          }
        }

        string packetString = string(buffer);
        pair<string, vector<int>> receivedPacket = parsePacketString(packetString);
        string packetType = receivedPacket.first;
        vector<int> msg = receivedPacket.second;

        // Log the successful received packet
        string direction = "Received";
        printPacketMessage(direction, portToId[i], id, packetType, msg);

        if (packetType == "ACK") {
          ackReceived = true;
          counts.ack++;
        } else if (packetType == "ADD") {
          addReceived = true;

          FlowRule newRule;

          if (msg[0] == 0) {
            newRule = {0, MAX_IP, msg[1], msg[2], "DROP", msg[3], MIN_PRI, 1};
          } else if (msg[0] == 1) {
            newRule = {0, MAX_IP, msg[1], msg[2], "FORWARD", msg[3], MIN_PRI, 1};

            // Open FIFO for writing if not done so already
            if (!portToFd.count(msg[3])) {
              string relayFifo = makeFifoName(id, portToId[msg[3]]);
              int portFd = openFifo(relayFifo, O_WRONLY | O_NONBLOCK);
              pair<int, int> portConnection = make_pair(msg[3], portFd);
              portToFd.insert(portConnection);
            }

            // Ensure switch is not closed before sending
            if (find(closed.begin(), closed.end(), i) == closed.end()) {
              sendRelayPacket(portToFd[msg[3]], id, portToId[msg[3]], msg[4], msg[1]);
            }

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
          if (msg[1] < ipLow || msg[1] > ipHigh) {
            // Ensure switch is not closed before sending
            if (find(closed.begin(), closed.end(), i) == closed.end()) {
              if (id > i) {
                sendRelayPacket(portToFd[1], id, portToId[1], msg[0], msg[1]);
                counts.relayOut++;
              } else if (id < i) {
                sendRelayPacket(portToFd[2], id, portToId[2], msg[0], msg[1]);
                counts.relayOut++;
              }
            }
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