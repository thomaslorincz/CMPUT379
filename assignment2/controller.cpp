#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
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

// Table containing info about opened switches
vector<SwitchInfo> switch_info_table;

// Global count of packets seen
int cont_open_count = 0;
int cont_query_count = 0;
int cont_ack_count = 0;
int cont_add_count = 0;

// Mapping switch IDs to FDs
map<int, int> id_to_fd;

/**
 * List the controller specs including switches known and
 * packets seen.
 */
void ControllerList() {
  printf("Switch information:\n");
  for (auto &info : switch_info_table) {
    printf("[sw%i]: port1= %i, port2= %i, port3= %i-%i\n", info.id,
           info.port1Id, info.port2Id, info.ipLow, info.ipHigh);
  }
  printf("\n");
  printf("Packet stats:\n");
  printf("\tReceived:    OPEN:%i, QUERY:%i\n", cont_open_count,
         cont_query_count);
  printf("\tTransmitted: ACK:%i, ADD:%i\n", cont_ack_count, cont_add_count);
}

/**
 * Main controller event loop. Communicates with switches via FIFOs.
 */
void ControllerLoop(int num_switches) {
  struct pollfd pfds[num_switches + 2];
  pfds[0].fd = STDIN_FILENO;
  pfds[0].events = POLLIN;
  pfds[0].revents = 0;
  char buffer[MAX_BUFFER];

  // Create and open read FIFOS for all attached switches
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

  sigset_t sigset;
  sigemptyset(&sigset);
  sigaddset(&sigset, SIGUSR1);
  // Must block the signals in order for signalfd to receive them
  sigprocmask(SIG_BLOCK, &sigset, nullptr);
  if (errno) {
    perror("Error: Could not set signal mask.\n");
    exit(errno);
  }

  pfds[num_switches + 1].fd = signalfd(-1, &sigset, 0);
  pfds[num_switches + 1].events = POLLIN;
  pfds[num_switches + 1].revents = 0;

  while (true) {
    /*
     * 1. Poll the keyboard for a user command. The user can issue one of the
     * following commands. list: The program writes all entries in the flow
     * table, and for each transmitted or received packet type, the program
     * writes an aggregate count of handled packets of this type. exit: The
     * program writes the above information and exits.
     */
    poll(pfds, num_switches + 2, 0);  // Poll from all file descriptors
    if (errno) perror("Error: poll() failure.\n");
    errno = 0;

    if (pfds[0].revents & POLLIN) {
      ssize_t r = read(pfds[0].fd, buffer, MAX_BUFFER);
      if (!r) printf("Warning: stdin closed.\n");  // TODO: errno?

      string cmd = string(buffer);
      Trim(cmd);  // Trim whitespace

      if (cmd == "list") {
        ControllerList();
      } else if (cmd == "exit") {
        ControllerList();
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
        pair<string, vector<int>> received_packet =
            ParsePacketString(packet_string);
        string packet_type = get<0>(received_packet);
        vector<int> packet_message = get<1>(received_packet);

        printf("Received packet: %s\n", buffer);

        if (packet_type == "OPEN") {
          cont_open_count++;

          if (packet_message.size() < 5) {
            printf("Error: Invalid OPEN packet.\n");
            continue;
          }

          switch_info_table.push_back({packet_message[0], packet_message[1],
                                       packet_message[2], packet_message[3],
                                       packet_message[4]});

          // Returns lowest unused file descriptor on success
          string fifo_name = MakeFifoName(CONTROLLER_ID, i);
          int fd = open(fifo_name.c_str(), O_WRONLY | O_NONBLOCK);
          if (errno) perror("Error: Could not open FIFO.\n");
          errno = 0;
          id_to_fd.insert({i, fd});

          string ack_message = "ACK:";

          write(fd, ack_message.c_str(), strlen(ack_message.c_str()));
          if (errno) perror("Error: Could not write.\n");
          errno = 0;

          cont_ack_count++;
        } else if (packet_type == "QUERY") {
          cont_query_count++;

          int query_ip = packet_message[0];
          if (query_ip > MAXIP || query_ip < 0) {
            printf("Error: Invalid IP for QUERY. Dropping.\n");
            continue;
          }

          // Check for information in the switch info table
          bool found = false;
          for (auto &info : switch_info_table) {
            if (query_ip >= info.ipLow && query_ip <= info.ipHigh) {
              int relay_id = 0;

              // Determine relay port
              // NOTE: Assumes ports are ordered
              if (info.id > i) {
                relay_id = 2;
              } else {
                relay_id = 1;
              }

              // Send new rule
              string add_message = "ADD:1," + to_string(info.ipLow) + "," +
                                   to_string(info.ipHigh) + "," +
                                   to_string(relay_id);

              write(id_to_fd[i], add_message.c_str(),
                    strlen(add_message.c_str()));
              if (errno) perror("Error: Could not write.\n");
              errno = 0;

              found = true;
              break;
            }
          }

          // If nothing is found in the info table, tell the switch to drop
          if (!found) {
            string add_message =
                "ADD:0," + to_string(query_ip) + "," + to_string(query_ip);
            write(id_to_fd[i], add_message.c_str(),
                  strlen(add_message.c_str()));
            if (errno) perror("Error: Could not write.\n");
            errno = 0;
          }

          cont_add_count++;
        } else {
          printf("Received %s packet. Ignored.\n", packet_type.c_str());
        }
      }
    }

    /*
     * In addition, upon receiving signal USER1, the switch displays the
     * information specified by the list command.
     */
    if (pfds[num_switches + 1].revents & POLLIN) {
      struct signalfd_siginfo info {};
      ssize_t r = read(pfds[num_switches + 2].fd, &info, sizeof(info));
      if (!r) {
        printf("Warning: Signal reading error.\n");
      }
      unsigned sig = info.ssi_signo;
      if (sig == SIGUSR1) {
        ControllerList();
      }
    }

    memset(buffer, 0, sizeof(buffer));  // Clear buffer
  }
}