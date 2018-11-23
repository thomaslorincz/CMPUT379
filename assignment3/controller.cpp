#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <algorithm>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include "util.h"

#define CONTROLLER_ID 0
#define MAX_BUFFER 1024
#define MAX_IP 1000

using namespace std;

/**
 * A struct for storing the controller's packet counts
 */
typedef struct {
    int open;
    int query;
    int add;
    int ack;
} ControllerPacketCounts;

/**
 * A struct that represents the information of a switch
 */
typedef struct {
    int id;
    int port1Id;
    int port2Id;
    int ipLow;
    int ipHigh;
} SwitchInfo;

/**
 * Function used to close all FD connections before exiting.
 */
void cleanup(int numSwitches, pollfd pfds[]) {
  for (int i = 0; i < numSwitches + 2; i++) close(pfds[i].fd);
  exit(EXIT_SUCCESS);
}

/**
 * Sends an ACK packet to a connected switch.
 */
void sendAckPacket(int numSwitches, pollfd pfds[], int fd, int destId) {
  string ackString = "ACK:";
  write(fd, ackString.c_str(), strlen(ackString.c_str()));
  if (errno) {
    perror("write() failure");
    cleanup(numSwitches, pfds);
    exit(errno);
  }

  // Log the successful packet transmission
  string direction = "Transmitted";
  string type = "ACK";
  pair<string, vector<int>> parsedPacket = parsePacketString(ackString);
  printPacketMessage(direction, 0, destId, type, parsedPacket.second);
}

/**
 * Sends an ADD packet to a connected switch.
 */
void sendAddPacket(int numSwitches, pollfd pfds[], int fd, int destId, int action, int ipLow,
                   int ipHigh, int relayPort, int srcIp) {
  string addString = "ADD:" + to_string(action) + "," + to_string(ipLow) + "," + to_string(ipHigh)
                     + "," + to_string(relayPort) + "," + to_string(srcIp);
  write(fd, addString.c_str(), strlen(addString.c_str()));
  if (errno) {
    perror("Failed to write");
    cleanup(numSwitches, pfds);
    exit(errno);
  }

  // Log the successful packet transmission.
  string direction = "Transmitted";
  string type = "ADD";
  pair<string, vector<int>> parsedPacket = parsePacketString(addString);
  printPacketMessage(direction, 0, destId, type, parsedPacket.second);
}

/**
 * List the controller status information including switches known and packets seen.
 */
void controllerList(vector<SwitchInfo> switchInfoTable, ControllerPacketCounts &counts) {
  printf("Switch information:\n");
  for (auto &info : switchInfoTable) {
    printf("[sw%i]: port1= %i, port2= %i, port3= %i-%i\n", info.id, info.port1Id, info.port2Id,
           info.ipLow, info.ipHigh);
  }
  printf("\n");
  printf("Packet stats:\n");
  printf("\tReceived:    OPEN:%i, QUERY:%i\n", counts.open, counts.query);
  printf("\tTransmitted: ACK:%i, ADD:%i\n", counts.ack, counts.add);
}

/**
 * Main controller event loop. Communicates with switches via TCP sockets.
 */
void controllerLoop(int numSwitches, uint16_t portNumber) {
  // Table containing info about opened switches
  vector<SwitchInfo> switchInfoTable;

  // Mapping switch IDs to FDs
  map<int, int> idToFd;

  // Counts of each type of packet seen
  ControllerPacketCounts counts = {0, 0, 0, 0};

  // Set up indices for easy reference
  int pfdsSize = numSwitches + 2;
  int mainSocket = pfdsSize - 1;

  struct pollfd pfds[pfdsSize];
  char buffer[MAX_BUFFER];

  // Set up STDIN for polling from
  pfds[0].fd = STDIN_FILENO;
  pfds[0].events = POLLIN;
  pfds[0].revents = 0;

  int sockets[numSwitches + 1];
  int socketIdx = 1;
  struct sockaddr_in sin {}, from {};
  socklen_t sinLength = sizeof(sin), fromLength = sizeof(from);

  // Create a managing socket
  if ((pfds[mainSocket].fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("Error: Could not create socket.\n");
    cleanup(numSwitches, pfds);
    exit(errno);
  }
  // Prepare for non-blocking I/O polling from the managing socket
  pfds[mainSocket].events = POLLIN;
  pfds[mainSocket].revents = 0;

  // Set socket options
  int opt = 1;
  if (setsockopt(pfds[mainSocket].fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
    perror("Error: Could not set socket options.\n");
    cleanup(numSwitches, pfds);
    exit(errno);
  }

  // Bind the managing socket to a name
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = htonl(INADDR_ANY);
  sin.sin_port = htons(portNumber);

  if (bind(pfds[mainSocket].fd, (struct sockaddr *) &sin, sinLength) < 0) {
    perror("bind() failure");
    cleanup(numSwitches, pfds);
    exit(errno);
  }

  // Indicate how many connection requests can be queued
  if (listen(pfds[mainSocket].fd, numSwitches) < 0) {
    perror("listen() failure");
    cleanup(numSwitches, pfds);
    exit(errno);
  }

  vector<int> closed; // Keeps track of closed switches

  while (true) {
    /*
     * 1. Poll the keyboard for a user command. The user can issue one of the following commands.
     * list: The program writes all entries in the flow table, and for each transmitted or received
     * packet type, the program writes an aggregate count of handled packets of this type.
     * exit: The program writes the above information and exits.
     */
    if (poll(pfds, (nfds_t) pfdsSize, 0) == -1) { // Poll from all file descriptors
      perror("poll() failure");
      cleanup(numSwitches, pfds);
      exit(errno);
    }

    if (pfds[0].revents & POLLIN) {
      if (!read(pfds[0].fd, buffer, MAX_BUFFER)) {
        printf("Error: stdin closed.\n");
        exit(EXIT_FAILURE);
      }

      string cmd = string(buffer);
      trim(cmd); // Trim whitespace

      if (cmd == "list") {
        controllerList(switchInfoTable, counts);
      } else if (cmd == "exit") {
        controllerList(switchInfoTable, counts);
        cleanup(numSwitches, pfds);
        exit(EXIT_SUCCESS);
      } else {
        printf("Error: Unrecognized command. Please use \"list\" or \"exit\".\n");
      }
    }

    memset(buffer, 0, sizeof(buffer)); // Clear buffer

    /*
     * 2. Poll the incoming FDs from the attached switches. The controller handles each incoming
     * packet, as described in the Packet Types section.
     */
    for (int i = 1; i <= numSwitches; i++) {
      if (pfds[i].revents & POLLIN) {
        // Check if the connection has closed
        if (!read(pfds[i].fd, buffer, MAX_BUFFER)) {
          printf("Warning: Connection to sw%d closed.\n", i);
          close(pfds[i].fd);
          closed.push_back(i);
          continue;
        }

        string packetString = string(buffer);
        pair<string, vector<int>> receivedPacket = parsePacketString(packetString);
        string packetType = get<0>(receivedPacket);
        vector<int> packetMessage = get<1>(receivedPacket);

        // Log the successful received packet
        string direction = "Received";
        printPacketMessage(direction, i, CONTROLLER_ID, packetType, packetMessage);

        if (packetType == "OPEN") {
          counts.open++;
          switchInfoTable.push_back({packetMessage[0], packetMessage[1], packetMessage[2],
                                     packetMessage[3], packetMessage[4]});
          idToFd.insert({i, pfds[i].fd});
          // Ensure switch is not closed before sending
          if (find(closed.begin(), closed.end(), i) == closed.end()) {
            sendAckPacket(numSwitches, pfds, pfds[i].fd, i);
          }
          counts.ack++;
        } else if (packetType == "QUERY") {
          counts.query++;

          int srcIp = packetMessage[0];
          if (srcIp > MAX_IP || srcIp < 0) {
            printf("Error: Invalid IP for QUERY. Dropping.\n");
            continue;
          }

          int destIp = packetMessage[1];
          if (destIp > MAX_IP || destIp < 0) {
            printf("Error: Invalid IP for QUERY. Dropping.\n");
            continue;
          }

          // Check for information in the switch info table
          bool found = false;
          for (auto &info : switchInfoTable) {
            if (destIp >= info.ipLow && destIp <= info.ipHigh) {
              found = true;

              int relayPort = 0;

              // Determine relay port
              if (info.id > i) {
                relayPort = 2;
              } else {
                relayPort = 1;
              }

              // Ensure switch is not closed before sending
              if (find(closed.begin(), closed.end(), i) == closed.end()) {
                // Send new rule
                sendAddPacket(numSwitches, pfds, idToFd[i], i, 1, info.ipLow, info.ipHigh,
                              relayPort, srcIp);
              }

              break;
            }
          }

          // If nothing is found in the info table, tell the switch to drop
          if (!found) {
            // Ensure switch is not closed before sending
            if (find(closed.begin(), closed.end(), i) == closed.end()) {
              sendAddPacket(numSwitches, pfds, idToFd[i], i, 0, destIp, destIp, 0, srcIp);
            }
          }

          counts.add++;
        } else {
          printf("Received %s packet. Ignored.\n", packetType.c_str());
        }
      }
    }

    memset(buffer, 0, sizeof(buffer)); // Clear buffer

    // Check the socket file descriptor for events
    if (pfds[mainSocket].revents & POLLIN) {
      if ((sockets[socketIdx] =
               accept(pfds[mainSocket].fd, (struct sockaddr *) &from, &fromLength)) < 0) {
        perror("accept() failure");
        cleanup(numSwitches, pfds);
        exit(errno);
      }
      pfds[socketIdx].fd = sockets[socketIdx];
      pfds[socketIdx].events = POLLIN;
      pfds[socketIdx].revents = 0;

      // Set socket to non-blocking
      if (fcntl(pfds[socketIdx].fd, F_SETFL, fcntl(pfds[socketIdx].fd, F_GETFL) | O_NONBLOCK) < 0) {
        perror("fcntl() failure");
        exit(errno);
      }

      socketIdx++;
    }

    memset(buffer, 0, sizeof(buffer)); // Clear buffer
  }
}