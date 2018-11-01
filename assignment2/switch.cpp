#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fstream>
#include <tuple>
#include <vector>
#include "util.h"

#define CONTROLLER_ID 0
#define MAXIP 1000
#define MINPRI 4
#define MAX_BUFFER 1024

using namespace std;

typedef struct {
  int srcIP_lo;
  int srcIP_hi;
  int destIP_lo;
  int destIP_hi;
  string actionType;  // FORWARD, DROP
  int actionVal;
  int pri;  // 0, 1, 2, 3, 4 (highest - lowest)
  int pktCount;
} flow_rule;

typedef struct {
  int fd;
  string fifo;
} connection;

void SwitchList(vector<flow_rule> flow_table, int admit, int ack, int addRule,
                int relayIn, int open, int query, int relayOut) {
  printf("Flow table:\n");
  int i = 0;
  for (auto &rule : flow_table) {
    printf(
        "[%i] (srcIp= %i=%i, destIp= %i-%i, action= %s:%i, pri= %i, pktCount= "
        "%i)\n",
        i, rule.srcIP_lo, rule.srcIP_hi, rule.destIP_lo, rule.destIP_hi,
        rule.actionType.c_str(), rule.actionVal, rule.pri, rule.pktCount);
    i++;
  }
  printf("\n");
  printf("Packet Stats:\n");
  printf("\tReceived:    ADMIT:%i, ACK:%i, ADDRULE:%i, RELAYIN:%i\n", admit,
         ack, addRule, relayIn);
  printf("\tTransmitted: OPEN: %i, QUERY:%i, RELAYOUT: %i\n", open, query,
         relayOut);
}

void SwitchLoop(int id, int port_1_id, int port_2_id, tuple<int, int> ip_range,
                ifstream &in) {
  vector<flow_rule> flow_table;
  flow_rule initial_rule = {
      0, MAXIP, get<0>(ip_range), get<1>(ip_range), "FORWARD", 3, MINPRI, 0};
  flow_table.push_back(initial_rule);

  int receivers = 1;
  receivers = (port_1_id != -1) ? receivers + 1 : receivers;
  receivers = (port_2_id != -1) ? receivers + 1 : receivers;

  int admitCount = 0;
  int ackCount = 0;
  int addRuleCount = 0;
  int relayInCount = 0;
  int openCount = 0;
  int queryCount = 0;
  int relayOutCount = 0;

  vector<connection> send_connections;

  char buffer[MAX_BUFFER];
  struct pollfd pfds[receivers + 1];
  pfds[0].fd = STDIN_FILENO;
  pfds[0].events = POLLIN;
  pfds[0].revents = 0;

  string read_fifo_name = MakeFifoName(CONTROLLER_ID, id);

  mkfifo(read_fifo_name.c_str(),
          S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
  if (errno) perror("Error: Could not create a FIFO connection.\n");
  errno = 0;

  // Returns lowest unused file descriptor on success
  int read_fd = open(read_fifo_name.c_str(), O_RDONLY | O_NONBLOCK);
  if (errno) perror("Error: Could not open FIFO.\n");
  errno = 0;

  printf("Created %s fd = %i\n", read_fifo_name.c_str(), read_fd);

  pfds[1].fd = read_fd;
  pfds[1].events = POLLIN;
  pfds[1].revents = 0;

  string write_fifo_name = MakeFifoName(id, CONTROLLER_ID);

  // Returns lowest unused file descriptor on success
  int write_fd = open(write_fifo_name.c_str(), O_WRONLY);
  if (errno) perror("Error: Could not open FIFO.\n");
  errno = 0;

  // Send an OPEN packet to the controller
  string open_message = to_string(OPEN) + ":" + to_string(id) + "," +
                        to_string(port_1_id) + "," + to_string(port_2_id) +
                        "," + to_string(get<0>(ip_range)) + "," +
                        to_string(get<1>(ip_range));
  write(write_fd, open_message.c_str(), strlen(open_message.c_str()));
  if (errno) perror("Error: Failed to write.\n");
  errno = 0;

  while (true) {
    poll(pfds, receivers + 1, 0);
    if (errno) perror("Error: poll() failure.\n");
    errno = 0;

    if (pfds[0].revents & POLLIN) {
      ssize_t r = read(pfds[0].fd, buffer, MAX_BUFFER);
      if (!r) printf("Warning: stdin closed.\n");

      string cmd = string(buffer);
      Trim(cmd);  // Trim whitespace

      if (cmd == "list") {
        SwitchList(flow_table, admitCount, ackCount, addRuleCount, relayInCount,
                   openCount, queryCount, relayOutCount);
      } else if (cmd == "exit") {
        SwitchList(flow_table, admitCount, ackCount, addRuleCount, relayInCount,
                   openCount, queryCount, relayOutCount);
        exit(0);
      } else {
        printf("Error: Unrecognized command. Please use list or exit.\n");
      }
    }

    /*
     * 3. Poll the incoming FIFOs from the controller and the attached switches.
     * The switch handles each incoming packet, as described in the Packet Types
     * section.
     */
    for (int i = 1; i <= receivers; i++) {
      if (pfds[1].revents & POLLIN) {
        ssize_t r = read(pfds[1].fd, buffer, MAX_BUFFER);
        if (!r) {
          printf("Warning: Connection closed.\n");
        }
        string packet_string = string(buffer);
        Packet received_packet = ParsePacketString(packet_string);

        printf("Received packet: %s\n", buffer);

        if (received_packet.type == ACK) {
          ackCount++;
        } else if (received_packet.type == ADD) {
          // Ignore for now
        } else if (received_packet.type == RELAY) {
          // Ignore for now
        } else {
          printf("Received %s packet. Ignored.\n",
                  to_string(received_packet.type).c_str());
        }
      }
    }

    memset(buffer, 0, sizeof(buffer));  // Clear buffer
  }
}