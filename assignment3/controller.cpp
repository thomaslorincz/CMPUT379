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
#define MAXIP 1000

using namespace std;

typedef struct {
  int id;
  int port1Id;
  int port2Id;
  int ipLow;
  int ipHigh;
} SwitchInfo;

void cleanup(int numSwitches, int *sockets, pollfd *pfds, int mainSocketIdx) {
  // Clean up sockets
  for (int i = 1; i < numSwitches + 1; i++) close(sockets[i]);
  close(pfds[mainSocketIdx].fd);
  exit(errno);
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
    cleanup(numSwitches, sockets, pfds, mainSocketIdx); // TODO: Put this everywhere
    exit(errno);
  }
  // Prepare for non-blocking I/O polling from the managing socket
  pfds[mainSocketIdx].events = POLLIN;
  pfds[mainSocketIdx].revents = 0;

  // Set socket options
  int opt = 1;
  if (setsockopt(pfds[mainSocketIdx].fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
    perror("Error: Could not set socket options.\n");
    cleanup(numSwitches, sockets, pfds, mainSocketIdx);
    exit(errno);
  }

  // Bind the managing socket to a name
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = htonl(INADDR_ANY);
  sin.sin_port = htons(portNumber);

  if (bind(pfds[mainSocketIdx].fd, (struct sockaddr *) &sin, sinLength) < 0) {
    perror("Error: Bind failure.\n");
    cleanup(numSwitches, sockets, pfds, mainSocketIdx);
    exit(errno);
  }

  // Indicate how many connection requests can be queued
  if (listen(pfds[mainSocketIdx].fd, numSwitches) < 0) {
    perror("Error: Listen failure.\n");
    cleanup(numSwitches, sockets, pfds, mainSocketIdx);
    exit(errno);
  }

  while (true) {
    /**
     * 1. Poll the keyboard for a user command. The user can issue one of the
     * following commands. list: The program writes all entries in the flow
     * table, and for each transmitted or received packet type, the program
     * writes an aggregate count of handled packets of this type. exit: The
     * program writes the above information and exits.
     */
    if (poll(pfds, (nfds_t) pfdsSize, 0) == -1) { // Poll from all file descriptors
      perror("Error: poll() failure.\n");
      cleanup(numSwitches, sockets, pfds, mainSocketIdx);
      exit(errno);
    }

    if (pfds[0].revents & POLLIN) {
      if (!read(pfds[0].fd, buffer, MAX_BUFFER)) {
        printf("Warning: stdin closed.\n");  // TODO: errno?
      }

      string cmd = string(buffer);
      trim(cmd);  // trim whitespace

      if (cmd == "list") {
        controllerList(switchInfoTable, openCount, queryCount, ackCount, addCount);
      } else if (cmd == "exit") {
        controllerList(switchInfoTable, openCount, queryCount, ackCount, addCount);
        cleanup(numSwitches, sockets, pfds, mainSocketIdx);
        exit(EXIT_SUCCESS);
      } else {
        printf("Error: Unrecognized command. Please use 'list' or 'exit'.\n");
      }
    }

    memset(buffer, 0, sizeof(buffer));  // Clear buffer

    /**
     * 2. Poll the incoming FIFOs from the attached switches. The controller
     * handles each incoming packet, as described in the Packet Types section.
     */
    for (int i = 1; i <= numSwitches; i++) {
      if (pfds[i].revents & POLLIN) {
        if (!read(pfds[i].fd, buffer, MAX_BUFFER)) {
          printf("Warning: Connection closed.\n"); // TODO: Error checking?
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

          string fifoName = makeFifoName(CONTROLLER_ID, i);
          int fd = open(fifoName.c_str(), O_WRONLY | O_NONBLOCK);
          if (errno) {
            perror("Error: Could not open FIFO.\n");
            cleanup(numSwitches, sockets, pfds, mainSocketIdx);
            exit(errno);
          }
          idToFd.insert({i, fd});

          string ackMessage = "ACK:";

          // Write the ACK message
          write(fd, ackMessage.c_str(), strlen(ackMessage.c_str()));
          if (errno) {
            perror("Error: Could not write.\n");
            cleanup(numSwitches, sockets, pfds, mainSocketIdx);
            exit(errno);
          }

          ackCount++;
        } else if (packetType == "QUERY") {
          queryCount++;

          int queryIp = packetMessage[0];
          if (queryIp > MAXIP || queryIp < 0) {
            printf("Error: Invalid IP for QUERY. Dropping.\n");
            continue;
          }

          // Check for information in the switch info table
          bool found = false;
          for (auto &info : switchInfoTable) {
            if (queryIp >= info.ipLow && queryIp <= info.ipHigh) {
              int relayId = 0;

              // Determine relay port
              // NOTE: Assumes switches are ordered
              if (info.id > i) {
                relayId = 2;
              } else {
                relayId = 1;
              }

              // Send new rule
              string add_message = "ADD:1," + to_string(info.ipLow) + "," + to_string(info.ipHigh)
                  + "," + to_string(relayId);

              write(idToFd[i], add_message.c_str(), strlen(add_message.c_str()));
              if (errno) {
                perror("Error: Could not write.\n");
                cleanup(numSwitches, sockets, pfds, mainSocketIdx);
                exit(errno);
              }

              found = true;
              break;
            }
          }

          // If nothing is found in the info table, tell the switch to drop
          if (!found) {
            string addMessage = "ADD:0," + to_string(queryIp) + "," + to_string(queryIp);
            write(idToFd[i], addMessage.c_str(), strlen(addMessage.c_str()));
            if (errno) {
              perror("Error: Could not write.\n");
              cleanup(numSwitches, sockets, pfds, mainSocketIdx);
              exit(errno);
            }
          }

          addCount++;
        } else {
          printf("Received %s packet. Ignored.\n", packetType.c_str());
        }
      }
    }

    memset(buffer, 0, sizeof(buffer));  // Clear buffer

    /**
     * Check the socket file descriptor for events.
     */
    if (pfds[mainSocketIdx].revents & POLLIN) {
      if ((sockets[socketIdx] =
           accept(pfds[mainSocketIdx].fd, (struct sockaddr*) &from, &fromLength)) < 0) {
        perror("Error: Could not accept connection.\n");
        cleanup(numSwitches, sockets, pfds, mainSocketIdx);
        exit(errno);
      }
      pfds[socketIdx].fd = sockets[socketIdx];
      pfds[socketIdx].events = POLLIN;
      pfds[socketIdx].revents = 0;
      printf("INFO: new client connection, socket fd:%d , ip:%s , port:%hu\n",
             pfds[socketIdx].fd, inet_ntoa(sin.sin_addr) , ntohs (sin.sin_port));
    }
  }
}