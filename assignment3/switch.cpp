#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
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

#define CONTROLLER_ID 0
#define MAXIP 1000
#define MINPRI 4
#define MAX_BUFFER 1024

using namespace std;

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
 * Opens a FIFO for reading or writing.
 */
int openFifo(string &fifo_name, int flag) {
  // Returns lowest unused file descriptor on success
  int fd = open(fifo_name.c_str(), flag);
  if (errno) {
    perror("Error: Could not open FIFO.\n");
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
    perror("Error: Could not create a FIFO connection.\n");
    exit(errno);
  }

  int fd = openFifo(fifoName, flag);

  printf("Created %s fd = %i\n", fifoName.c_str(), fd);

  return fd;
}

/**
 * Handles an incoming packet. Based on its contents, the packet will either be ignored, dropped, or
 * forwarded.
 */
void handlePacketUsingFlowTable(vector<FlowRule> &flowTable, map<int, int> &portToFd,
                                map<int, int> &portToId, int switchId, int destIp,
                                int &relayOutCount, int &queryCount) {
  bool found = false;
  for (auto &rule : flowTable) {
    if (destIp >= rule.destIpLow && destIp <= rule.destIpHigh) {
      rule.pktCount++;
      if (rule.actionType == "DROP") {
        break;
      } else if (rule.actionType == "FORWARD") {
        if (rule.actionVal != 3) {
          string relayString = "RELAY:" + to_string(destIp);

          // Open the FIFO for writing if not done already
          if (!portToFd.count(rule.actionVal)) {
            string relayFifo = makeFifoName(switchId, rule.actionVal);
            int portFd = openFifo(relayFifo, O_WRONLY | O_NONBLOCK);

            pair<int, int> portConn = make_pair(portToId[rule.actionVal], portFd);
            portToFd.insert(portConn);
          }

          write(portToFd[rule.actionVal], relayString.c_str(), strlen(relayString.c_str()));
          if (errno) {
            perror("Error: Failed to write.\n");
            exit(errno);
          }

          relayOutCount++;
        }
      }

      found = true;
      break;
    }
  }

  if (!found) {
    int queryFd = portToFd[CONTROLLER_ID];
    string queryString = "QUERY:" + to_string(destIp);
    write(queryFd, queryString.c_str(), strlen(queryString.c_str()));
    if (errno) {
      perror("Error: Failed to write.\n");
      exit(errno);
    }

    queryCount++;
  }
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

  if (tokens[0] == "#") {
    return make_tuple(-1, -1, -1);
  } else {
    id = parseSwitchId(tokens[0]);

    srcIp = (int) strtol(tokens[1].c_str(), (char **) nullptr, 10);
    if (srcIp < 0 || srcIp > MAXIP || errno) {
      printf("Error: Invalid IP lower bound.\n");
      errno = 0;
      return make_tuple(-1, -1, -1);
    }

    destIp = (int) strtol(tokens[2].c_str(), (char **) nullptr, 10);
    if (destIp < 0 || destIp > MAXIP || errno) {
      printf("Error: Invalid IP lower bound.\n");
      errno = 0;
      return make_tuple(-1, -1, -1);
    }
  }

  for (auto &token : tokens) {
    printf("%s ", token.c_str());
  }
  printf("\n");

  return make_tuple(id, srcIp, destIp);
}

/**
 * List the status information of the switch.
 */
void switchList(vector<FlowRule> &flowTable, int admitCount, int ackCount, int addCount,
                int relayInCount, int openCount, int queryCount, int relayOutCount) {
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
  printf("\tReceived:    ADMIT:%i, ACK:%i, ADDRULE:%i, RELAYIN:%i\n", admitCount, ackCount,
         addCount, relayInCount);
  printf("\tTransmitted: OPEN: %i, QUERY:%i, RELAYOUT: %i\n", openCount, queryCount, relayOutCount);
}

/**
 * Main event loop for the switch. Polls all input FIFOS.
 * Sends and receives packets of varying types.
 */
void switchLoop(int id, int port1Id, int port2Id, int ipLow, int ipHigh, ifstream &in,
                string &ipAdress, uint16_t portNumber) {
  vector<FlowRule> flowTable; // Flow rule table
  flowTable.push_back({0, MAXIP, ipLow, ipHigh, "FORWARD", 3, MINPRI, 0}); // Add initial rule

  map<int, int> portToFd; // Map port number to FD
  map<int, int> portToId; // Map port number to switch ID

  // Counts of all seen packets
  int admitCount = 0;
  int ackCount = 0;
  int addCount = 0;
  int relayInCount = 0;
  int openCount = 0;
  int queryCount = 0;
  int relayOutCount = 0;

  int receivers = 1; // Must at least be connected to controller
  receivers = (port1Id != -1) ? receivers + 1 : receivers;
  receivers = (port2Id != -1) ? receivers + 1 : receivers;
  int pfdsSize = receivers + 2;
  int socketIdx = pfdsSize - 1;

  char buffer[MAX_BUFFER];
  struct pollfd pfds[pfdsSize];

  // Set up STDIN for polling from
  pfds[0].fd = STDIN_FILENO;
  pfds[0].events = POLLIN;
  pfds[0].revents = 0;

  struct sockaddr_in server {};

  // Creating socket file descriptor
  if ((pfds[socketIdx].fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
    perror("Error: Could not create socket.");
    exit(errno);
  }
  memset(&server, 0, sizeof(server));

  server.sin_family = AF_INET;
  server.sin_port = htons(portNumber);

  // Convert IPv4 and IPv6 addresses from text to binary form
  if (inet_pton(AF_INET, ipAdress.c_str(), &server.sin_addr) <= 0) {
    perror("Error: Invalid IP address.");
    // TODO: Cleanup?
    exit(errno);
  }

  if (connect(pfds[socketIdx].fd, (struct sockaddr *) &server, sizeof(server)) < 0) {
    perror("Error: Connections failed.");
    // TODO: Cleanup?
    exit(errno);
  }

  // Send an OPEN packet to the controller
  string openPacket = "OPEN:" + to_string(id) + "," + to_string(port1Id) + "," + to_string(port2Id)
      + "," + to_string(ipLow) + "," + to_string(ipHigh);
  write(pfds[socketIdx].fd, openPacket.c_str(), strlen(openPacket.c_str()));

  int pfdIndex = 1;

  // TODO: Not DRY. Make a function.
  // Create and open a reading FIFO for port 1 if not null
  if (port1Id != -1) {
    pair<int, int> port1Connection = make_pair(1, port1Id);
    portToId.insert(port1Connection);
    int port1Fd = createFifo(port1Id, id, O_RDONLY | O_NONBLOCK);
    pfds[pfdIndex].fd = port1Fd;
    pfds[pfdIndex].events = POLLIN;
    pfds[pfdIndex].revents = 0;
    pfdIndex++;
  }

  // Create and open a reading FIFO for port 2 if not null
  if (port2Id != -1) {
    pair<int, int> port2Connection = make_pair(2, port2Id);
    portToId.insert(port2Connection);
    int port2Fd = createFifo(port2Id, id, O_RDONLY | O_NONBLOCK);
    pfds[pfdIndex].fd = port2Fd;
    pfds[pfdIndex].events = POLLIN;
    pfds[pfdIndex].revents = 0;
  }

  while (true) {
    /**
     * 1. Read and process a single line from the traffic line (if the EOF has not been reached
     * yet). The switch ignores empty lines, comment lines, and lines specifying other handling
     * switches. A packet header is considered admitted if the line specifies the current switch.
     */
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
          admitCount++;
          handlePacketUsingFlowTable(flowTable, portToFd, portToId, id, destIp, relayOutCount,
                                     queryCount);
        }
      } else {
        in.close();
      }
    }

    // Poll all input FIFOs.
    // Delayed slightly (100ms) to wait for response packets from the controller.
    poll(pfds, (nfds_t) receivers + 2, 100);
    if (errno) {
      perror("Error: poll() failure.\n");
      exit(errno);
    }

    /**
     * 2. Poll the keyboard for a user command. The user can issue one of the following commands.
     * list: The program writes all entries in the flow table, and for each transmitted or received
     * packet type, the program writes an aggregate count of handled packets of this type. exit: The
     * program writes the above information and exits.
     */
    if (pfds[0].revents & POLLIN) {
      if (!read(pfds[0].fd, buffer, MAX_BUFFER)) { // TODO: Error checking?
        printf("Warning: stdin closed.\n");
      }

      string cmd = string(buffer);
      trim(cmd);  // trim whitespace

      if (cmd == "list") {
        switchList(flowTable, admitCount, ackCount, addCount, relayInCount, openCount, queryCount,
                   relayOutCount);
      } else if (cmd == "exit") {
        switchList(flowTable, admitCount, ackCount, addCount, relayInCount, openCount, queryCount,
                   relayOutCount);
        exit(EXIT_SUCCESS);
      } else {
        printf("Error: Unrecognized command. Please use list or exit.\n");
      }
    }

    /**
     * 3. Poll the incoming FIFOs from the controller and the attached switches. The switch handles
     * each incoming packet, as described in the Packet Types section.
     */
    for (int i = 1; i <= receivers; i++) {
      if (pfds[i].revents & POLLIN) {
        if (!read(pfds[i].fd, buffer, MAX_BUFFER)) { // TODO: Error checking?
          printf("Warning: Connection closed.\n");
        }
        string packetString = string(buffer);
        pair<string, vector<int>> receivedPacket = parsePacketString(packetString);
        string packetType = get<0>(receivedPacket);
        vector<int> packetMessage = get<1>(receivedPacket);

        printf("Received packet: %s\n", buffer);

        if (packetType == "ACK") {
          ackCount++;
        } else if (packetType == "ADD") {
          FlowRule newRule;
          string newAction;

          if (packetMessage[0] == 0) {
            newRule = {0, MAXIP, packetMessage[1], packetMessage[2], "DROP", 0, MINPRI, 1};
          } else if (packetMessage[0] == 1) {
            newRule = {0, MAXIP, packetMessage[1], packetMessage[2], "FORWARD", packetMessage[3],
                       MINPRI, 1};

            // Relay the message based on the new rule
            string relayString = "RELAY:" + to_string(packetMessage[1]);

            if (!portToFd.count(packetMessage[3])) {
              string relayFifo = makeFifoName(id, packetMessage[3]);
              int portFd = openFifo(relayFifo, O_WRONLY | O_NONBLOCK);

              pair<int, int> portConnection = make_pair(portToId[packetMessage[3]], portFd);
              portToFd.insert(portConnection);
            }

            write(portToFd[packetMessage[3]], relayString.c_str(), strlen(relayString.c_str()));
            if (errno) {
              perror("Error: Failed to write.\n");
              exit(errno);
            }
            relayOutCount++;
          } else {
            printf("Error: Invalid rule to add.\n");
            continue;
          }

          flowTable.push_back(newRule);
          addCount++;
        } else if (packetType == "RELAY") {
          relayInCount++;
          handlePacketUsingFlowTable(flowTable, portToFd, portToId, id, packetMessage[0],
                                     relayOutCount, queryCount);
        } else {
          // Unknown packet. Used for debugging.
          printf("Received %s packet. Ignored.\n", packetType.c_str());
        }
      }
    }

    memset(buffer, 0, sizeof(buffer));  // Clear buffer
  }
}