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
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include <netinet/in.h>
#include "util.h"

#define CONTROLLER_ID 0
#define MAX_BUFFER 1024
#define MAX_IP 1000

using namespace std;

typedef struct {
    int id;
    int port1Id;
    int port2Id;
    int ipLow;
    int ipHigh;
} SwitchInfo;

void cleanup(int numSwitches, pollfd pfds[]) {
  for (int i = 0; i < numSwitches + 2; i++) close(pfds[i].fd);
  exit(EXIT_SUCCESS);
}

/**
 * List the controller status information including switches known and packets seen.
 */
void controllerList(vector<SwitchInfo> switchInfoTable, int openCount, int queryCount,
                    int ackCount, int addCount) {
  printf("Switch information:\n");
  for (auto &info : switchInfoTable) {
    printf("[sw%i]: port1= %i, port2= %i, port3= %i-%i\n", info.id, info.port1Id, info.port2Id,
           info.ipLow, info.ipHigh);
  }
  printf("\n");
  printf("Packet stats:\n");
  printf("\tReceived:    OPEN:%i, QUERY:%i\n", openCount, queryCount);
  printf("\tTransmitted: ACK:%i, ADD:%i\n", ackCount, addCount);
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
  int openCount = 0;
  int queryCount = 0;
  int ackCount = 0;
  int addCount = 0;

  // Set up indices for easy reference
  int pfdsSize = numSwitches + 2;
  int mainSocketIdx = pfdsSize - 1;

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
  if ((pfds[mainSocketIdx].fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("Error: Could not create socket.\n");
    cleanup(numSwitches, pfds);
    exit(errno);
  }
  // Prepare for non-blocking I/O polling from the managing socket
  pfds[mainSocketIdx].events = POLLIN;
  pfds[mainSocketIdx].revents = 0;

  // Set socket options
  int opt = 1;
  if (setsockopt(pfds[mainSocketIdx].fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
    perror("Error: Could not set socket options.\n");
    cleanup(numSwitches, pfds);
    exit(errno);
  }

  // Bind the managing socket to a name
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = htonl(INADDR_ANY);
  sin.sin_port = htons(portNumber);

  if (bind(pfds[mainSocketIdx].fd, (struct sockaddr *) &sin, sinLength) < 0) {
    perror("Error: Bind failure.\n");
    cleanup(numSwitches, pfds);
    exit(errno);
  }

  // Indicate how many connection requests can be queued
  if (listen(pfds[mainSocketIdx].fd, numSwitches) < 0) {
    perror("Error: Listen failure.\n");
    cleanup(numSwitches, pfds);
    exit(errno);
  }

  while (true) {
    /**
     * 1. Poll the keyboard for a user command. The user can issue one of the following commands.
     * list: The program writes all entries in the flow table, and for each transmitted or received
     * packet type, the program writes an aggregate count of handled packets of this type.
     * exit: The program writes the above information and exits.
     */
    if (poll(pfds, (nfds_t) pfdsSize, 0) == -1) { // Poll from all file descriptors
      perror("Error: poll() failure.\n");
      cleanup(numSwitches, pfds);
      exit(errno);
    }

    if (pfds[0].revents & POLLIN) {
      if (!read(pfds[0].fd, buffer, MAX_BUFFER)) {
        printf("Error: stdin closed.\n");
        exit(EXIT_FAILURE);
      }

      string cmd = string(buffer);
      trim(cmd);  // Trim whitespace

      if (cmd == "list") {
        controllerList(switchInfoTable, openCount, queryCount, ackCount, addCount);
      } else if (cmd == "exit") {
        controllerList(switchInfoTable, openCount, queryCount, ackCount, addCount);
        cleanup(numSwitches, pfds);
        exit(EXIT_SUCCESS);
      } else {
        printf("Error: Unrecognized command. Please use 'list' or 'exit'.\n");
      }
    }

    memset(buffer, 0, sizeof(buffer)); // Clear buffer

    /**
     * 2. Poll the incoming FIFOs from the attached switches. The controller
     * handles each incoming packet, as described in the Packet Types section.
     */
    for (int i = 1; i <= numSwitches; i++) {
      if (pfds[i].revents & POLLIN) {
        // Check if the connection has closed
        if (!read(pfds[i].fd, buffer, MAX_BUFFER)) {
          printf("Warning: Connection to sw%d closed.\n", i);
          close(pfds[i].fd);
          continue;
        }

        string packetString = string(buffer);
        pair<string, vector<int>> receivedPacket = parsePacketString(packetString);
        string packetType = get<0>(receivedPacket);
        vector<int> packetMessage = get<1>(receivedPacket);

        printf("Received packet: %s\n", buffer);

        if (packetType == "OPEN") {
          openCount++;

          if (packetMessage.size() < 5) {
            printf("Error: Invalid OPEN packet.\n");
            continue;
          }

          switchInfoTable.push_back({packetMessage[0], packetMessage[1], packetMessage[2],
                                     packetMessage[3], packetMessage[4]});

          idToFd.insert({i, pfds[socketIdx].fd});

          // Write the ACK message
          sendAckPacket(pfds[socketIdx].fd);
          if (errno) {
            cleanup(numSwitches, pfds);
            exit(errno);
          }

          ackCount++;
        } else if (packetType == "QUERY") {
          queryCount++;

          int queryIp = packetMessage[0];
          if (queryIp > MAX_IP || queryIp < 0) {
            printf("Error: Invalid IP for QUERY. Dropping.\n");
            continue;
          }

          // Check for information in the switch info table
          bool found = false;
          for (auto &info : switchInfoTable) {
            if (queryIp >= info.ipLow && queryIp <= info.ipHigh) {
              int relayId = 0;

              // Determine relay port
              if (info.id > i) {
                relayId = 2;
              } else {
                relayId = 1;
              }

              // Send new rule
              sendAddPacket(idToFd[i], 1, info.ipLow, info.ipHigh, relayId);
              if (errno) {
                cleanup(numSwitches, pfds);
                exit(errno);
              }

              found = true;
              break;
            }
          }

          // If nothing is found in the info table, tell the switch to drop
          if (!found) {
            sendAddPacket(idToFd[i], 0, queryIp, queryIp, 0);
            if (errno) {
              cleanup(numSwitches, pfds);
              exit(errno);
            }
          }

          addCount++;
        } else {
          printf("Received %s packet. Ignored.\n", packetType.c_str());
        }
      }
    }

    memset(buffer, 0, sizeof(buffer)); // Clear buffer

    // Check the socket file descriptor for events
    if (pfds[mainSocketIdx].revents & POLLIN) {
      if ((sockets[socketIdx] =
              accept(pfds[mainSocketIdx].fd, (struct sockaddr *) &from, &fromLength)) < 0) {
        perror("Error: Could not accept connection.");
        cleanup(numSwitches, pfds);
        exit(errno);
      }
      pfds[socketIdx].fd = sockets[socketIdx];
      pfds[socketIdx].events = POLLIN;
      pfds[socketIdx].revents = 0;
      printf("DEBUG: New client socket connection, fd:%d\n", pfds[socketIdx].fd);
    }

    memset(buffer, 0, sizeof(buffer)); // Clear buffer
  }
}