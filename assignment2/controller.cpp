#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include "util.h"

#define CONTROLLER_ID 0
#define MAX_BUFFER 1024

using namespace std;

typedef struct {
  int id;
  int port1Id;
  int port2Id;
  int ipLow;
  int ipHigh;
} SwitchInfo;

SwitchInfo ParseOpenMessage(string &m) {
  vector<int> vect;
  stringstream ss(m);

  // Split packet string into ints (comma delimited)
  int i = 0;
  while (ss >> i) {
    vect.push_back(i);
    if (ss.peek() == ',') ss.ignore();
  }

  if (vect.size() < 5) {
    printf("Error: Invalid OPEN packet.\n");
  }

  return {vect[0], vect[1], vect[2], vect[3], vect[4]};
}

void ControllerList(vector<SwitchInfo> switch_info_table, int open, int query,
                    int ack, int add) {
  printf("Switch information:\n");
  for (auto &info : switch_info_table) {
    printf("[sw%i]: port1= %i, port2= %i, port3= %i-%i\n", info.id,
           info.port1Id, info.port2Id, info.ipLow, info.ipHigh);
  }
  printf("\n");
  printf("Packet stats:\n");
  printf("\tReceived:    OPEN:%i, QUERY:%i\n", open, query);
  printf("\tTransmitted: ACK:%i, ADD:%i\n", ack, add);
}

void ControllerLoop(int num_switches) {
  vector<SwitchInfo> switch_info_table;
  int open_count = 0;
  int query_count = 0;
  int ack_count = 0;
  int add_count = 0;

  map<int, int> id_to_fd;  // Mapping switch IDs to FDs

  struct pollfd pfds[num_switches + 1];
  pfds[0].fd = STDIN_FILENO;
  pfds[0].events = POLLIN;
  pfds[0].revents = 0;
  char buffer[MAX_BUFFER];

  for (int i = 1; i <= num_switches; i++) {
    string fifo_name = MakeFifoName(i, CONTROLLER_ID);
    mkfifo(fifo_name.c_str(),
           S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    if (errno) perror("Error: Could not create a FIFO connection.\n");
    errno = 0;

    // Returns lowest unused file descriptor on success
    int fd = open(fifo_name.c_str(), O_RDONLY | O_NONBLOCK);
    if (errno) perror("Error: Could not open FIFO.\n");
    errno = 0;

    printf("Created %s fd = %i\n", fifo_name.c_str(), fd);

    pfds[i].fd = fd;
    pfds[i].events = POLLIN;
    pfds[i].revents = 0;
  }

  while (true) {
    /*
     * 1. Poll the keyboard for a user command. The user can issue one of the
     * following commands. list: The program writes all entries in the flow
     * table, and for each transmitted or received packet type, the program
     * writes an aggregate count of handled packets of this type. exit: The
     * program writes the above information and exits.
     */
    poll(pfds, num_switches + 1, 0);  // Poll from all file descriptors
    if (errno) perror("Error: poll() failure.\n");
    errno = 0;

    if (pfds[0].revents & POLLIN) {
      ssize_t r = read(pfds[0].fd, buffer, MAX_BUFFER);
      if (!r) printf("Warning: stdin closed.\n");  // TODO: errno?

      string cmd = string(buffer);
      Trim(cmd);  // Trim whitespace

      if (cmd == "list") {
        ControllerList(switch_info_table, open_count, query_count, ack_count,
                       add_count);
      } else if (cmd == "exit") {
        ControllerList(switch_info_table, open_count, query_count, ack_count,
                       add_count);
        exit(0);
      } else {
        printf("Error: Unrecognized command. Please use list or exit.\n");
      }
    }

    /*
     * 2. Poll the incoming FIFOs from the attached switches. The controller
     * handles each incoming packet, as described in the Packet Types section.
     */
    for (int i = 1; i <= num_switches; i++) {
      if (pfds[i].revents & POLLIN) {
        ssize_t r = read(pfds[i].fd, buffer, MAX_BUFFER);
        if (!r) {
          printf("Warning: Connection closed.\n");
        }
        string packet_string = string(buffer);
        Packet received_packet = ParsePacketString(packet_string);

        printf("Received packet: %s\n", buffer);

        if (received_packet.type == OPEN) {
          open_count++;

          SwitchInfo new_info = ParseOpenMessage(received_packet.message);
          switch_info_table.push_back(new_info);

          // Returns lowest unused file descriptor on success
          string fifo_name = MakeFifoName(CONTROLLER_ID, i);
          int fd = open(fifo_name.c_str(), O_WRONLY);
          if (errno) perror("Error: Could not open FIFO.\n");
          errno = 0;
          id_to_fd.insert({i, fd});

          string ack_message = to_string(ACK) + ":";

          write(fd, ack_message.c_str(), strlen(ack_message.c_str()));
          if (errno) perror("Error: Could not write.\n");
          errno = 0;

          ack_count++;
        } else if (received_packet.type == QUERY) {
          query_count++;
          // TODO: Reply with an ADD packet (message is the flow table rule)
          string add_message = to_string(ADD) + ":";

          write(id_to_fd[i], add_message.c_str(), strlen(add_message.c_str()));
          if (errno) perror("Error: Could not write.\n");
          errno = 0;

          add_count++;
        } else {
          printf("Received %s packet. Ignored.\n",
                 to_string(received_packet.type).c_str());
        }
      }
    }

    memset(buffer, 0, sizeof(buffer));  // Clear buffer
  }
}