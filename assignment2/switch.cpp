#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
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

tuple<int, int, int> ParseTrafficFileLine(string &line) {
  int id;
  int src_ip;
  int dest_ip;

  istringstream iss(line);
  vector<string> tokens{istream_iterator<string>{iss},
                        istream_iterator<string>{}};
  if (tokens[0] == "#") {
    return make_tuple(-1, -1, -1);
  } else {
    id = ParseSwitchId(tokens[0]);  // TODO: Different error handling?

    src_ip = (int)strtol(tokens[1].c_str(), (char **)NULL, 10);
    if (src_ip < 0 || src_ip > MAXIP || errno) {
      printf("Error: Invalid IP lower bound.\n");
      errno = 0;
      return make_tuple(-1, -1, -1);
    }

    dest_ip = (int)strtol(tokens[2].c_str(), (char **)NULL, 10);
    if (dest_ip < 0 || dest_ip > MAXIP || errno) {
      printf("Error: Invalid IP lower bound.\n");
      errno = 0;
      return make_tuple(-1, -1, -1);
    }
  }

  for (auto &token : tokens) {
    printf("%s ", token.c_str());
  }
  printf("\n");

  return make_tuple(id, src_ip, dest_ip);
}

void SwitchList(vector<flow_rule> flow_table, int admit, int ack, int addRule,
                int relayIn, int open, int query, int relayOut) {
  printf("Flow table:\n");
  int i = 0;
  for (auto &rule : flow_table) {
    printf("[%i] (srcIp= %i-%i, destIp= %i-%i, ", i, rule.srcIP_lo,
           rule.srcIP_hi, rule.destIP_lo, rule.destIP_hi);
    printf("action= %s:%i, pri= %i, pktCount= %i)\n", rule.actionType.c_str(),
           rule.actionVal, rule.pri, rule.pktCount);
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

  int admit_count = 0;
  int ack_count = 0;
  int add_rule_count = 0;
  int relay_in_count = 0;
  int open_count = 0;
  int query_count = 0;
  int relay_out_count = 0;

  map<int, connection> send_connections;

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

  send_connections.insert(
      pair<int, connection>(CONTROLLER_ID, {write_fd, write_fifo_name}));

  // Send an OPEN packet to the controller
  string open_message = "OPEN:" + to_string(id) + "," + to_string(port_1_id) +
                        "," + to_string(port_2_id) + "," +
                        to_string(get<0>(ip_range)) + "," +
                        to_string(get<1>(ip_range));
  write(write_fd, open_message.c_str(), strlen(open_message.c_str()));
  if (errno) perror("Error: Failed to write.\n");
  errno = 0;
  open_count++;

  // TODO: Refactor/simplify
  if (port_1_id != -1) {
    string port_1_fifo = MakeFifoName(port_1_id, id);

    mkfifo(port_1_fifo.c_str(),
           S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    if (errno) perror("Error: Could not create a FIFO connection.\n");
    errno = 0;

    // Returns lowest unused file descriptor on success
    int port_1_fd = open(port_1_fifo.c_str(), O_RDONLY | O_NONBLOCK);
    if (errno) perror("Error: Could not open FIFO.\n");
    errno = 0;

    send_connections.insert(
        pair<int, connection>(port_1_id, {port_1_fd, port_1_fifo}));

    printf("Created %s fd = %i\n", port_1_fifo.c_str(), port_1_fd);

    pfds[2].fd = port_1_fd;
    pfds[2].events = POLLIN;
    pfds[2].revents = 0;
  }

  // TODO: Refactor/simplify
  if (port_2_id != -1) {
    string port_2_fifo = MakeFifoName(port_2_id, id);

    mkfifo(port_2_fifo.c_str(),
           S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    if (errno) perror("Error: Could not create a FIFO connection.\n");
    errno = 0;

    // Returns lowest unused file descriptor on success
    int port_2_fd = open(port_2_fifo.c_str(), O_RDONLY | O_NONBLOCK);
    if (errno) perror("Error: Could not open FIFO.\n");
    errno = 0;

    send_connections.insert(
        pair<int, connection>(port_2_id, {port_2_fd, port_2_fifo}));

    printf("Created %s fd = %i\n", port_2_fifo.c_str(), port_2_fd);

    pfds[3].fd = port_2_fd;
    pfds[3].events = POLLIN;
    pfds[3].revents = 0;
  }

  while (true) {
    /**
     * 1. Read and process a single line from the traffic line (if the EOF has
     * not been reached yet). The switch ignores empty lines, comment lines, and
     * lines specifying other handling switches. A packet header is considered
     * admitted if the line specifies the current switch.
     */
    tuple<int, int, int> traffic_info;
    string line;
    if (in.is_open()) {
      if (getline(in, line)) {
        traffic_info = ParseTrafficFileLine(line);

        int traffic_id = get<0>(traffic_info);
        int src_ip = get<1>(traffic_info);
        int dest_ip = get<2>(traffic_info);

        if (id != traffic_id ||
            (traffic_id == -1 && src_ip == -1 && dest_ip == -1)) {
          // Ignore
        } else {
          admit_count++;
          bool found = false;
          for (auto &rule : flow_table) {
            if (src_ip >= rule.srcIP_lo && src_ip <= rule.srcIP_hi &&
                dest_ip >= rule.destIP_lo && dest_ip <= rule.destIP_hi) {
              rule.pktCount++;
              if (rule.actionType == "DROP") {
                // Drop
              } else if (rule.actionType == "FORWARD") {
                if (rule.actionVal != 3) {
                  int relay_fd = send_connections[rule.actionVal].fd;
                  string relay_string = "RELAY:";
                  write(relay_fd, relay_string.c_str(),
                        strlen(relay_string.c_str()));
                  if (errno) perror("Error: Failed to write.\n");
                  errno = 0;
                  relay_out_count++;
                }
              }

              found = true;
              break;
            }
          }

          if (!found) {
            int query_fd = send_connections[CONTROLLER_ID].fd;
            string query_string = "QUERY:" + to_string(dest_ip);
            write(query_fd, query_string.c_str(), strlen(query_string.c_str()));
            if (errno) perror("Error: Failed to write.\n");
            errno = 0;
            query_count++;
          }
        }
      } else {
        in.close();
      }
    }

    poll(pfds, receivers + 1, 20);  // Slight delay to wait for QUERY
    if (errno) perror("Error: poll() failure.\n");
    errno = 0;

    /*
     * 2. Poll the keyboard for a user command. The user can issue one of the
     * following commands. list: The program writes all entries in the flow
     * table, and for each transmitted or received packet type, the program
     * writes an aggregate count of handled packets of this type. exit: The
     * program writes the above information and exits.
     */
    if (pfds[0].revents & POLLIN) {
      ssize_t r = read(pfds[0].fd, buffer, MAX_BUFFER);
      if (!r) printf("Warning: stdin closed.\n");

      string cmd = string(buffer);
      Trim(cmd);  // Trim whitespace

      if (cmd == "list") {
        SwitchList(flow_table, admit_count, ack_count, add_rule_count,
                   relay_in_count, open_count, query_count, relay_out_count);
      } else if (cmd == "exit") {
        SwitchList(flow_table, admit_count, ack_count, add_rule_count,
                   relay_in_count, open_count, query_count, relay_out_count);
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

        if (received_packet.type == "ACK") {
          ack_count++;
        } else if (received_packet.type == "ADD") {
          vector<int> new_rule_specs =
              ParsePacketMessage(received_packet.message);
          flow_rule new_rule;
          string new_action;
          if (new_rule_specs[0] == 0) {
            new_rule = {0,
                        MAXIP,
                        new_rule_specs[1],
                        new_rule_specs[2],
                        "DROP",
                        0,
                        MINPRI,
                        1};
          } else if (new_rule_specs[0] == 1) {
            new_rule = {0,
                        MAXIP,
                        new_rule_specs[1],
                        new_rule_specs[2],
                        "FORWARD",
                        new_rule_specs[3],
                        MINPRI,
                        1};
          } else {
            printf("Error: Invalid rule to add.\n");
            continue;
          }

          flow_table.push_back(new_rule);
          add_rule_count++;
        } else if (received_packet.type == "RELAY") {
          relay_in_count++;
        } else {
          printf("Received %s packet. Ignored.\n",
                 received_packet.type.c_str());
        }
      }
    }

    memset(buffer, 0, sizeof(buffer));  // Clear buffer
  }
}