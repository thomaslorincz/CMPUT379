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

vector<flow_rule> flow_table;
map<int, int> port_to_fd;
map<int, int> port_to_id;

// Global counts of all packets
int admit_count = 0;
int ack_count = 0;
int add_rule_count = 0;
int relay_in_count = 0;
int open_count = 0;
int query_count = 0;
int relay_out_count = 0;

int OpenFifo(int src, int dest, int flag, string &fifo_name) {
  // Returns lowest unused file descriptor on success
  int fd = open(fifo_name.c_str(), flag);
  if (errno) perror("Error: Could not open FIFO.\n");
  errno = 0;

  return fd;
}

int CreateFifo(int src, int dest, int flag) {
  string fifo_name = MakeFifoName(src, dest);

  mkfifo(fifo_name.c_str(),
         S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
  if (errno) perror("Error: Could not create a FIFO connection.\n");
  errno = 0;

  int fd = OpenFifo(src, dest, flag, fifo_name);

  printf("Created %s fd = %i\n", fifo_name.c_str(), fd);

  return fd;
}

void HandlePacketUsingFlowTable(int switch_id, int dest_ip) {
  bool found = false;
  for (auto &rule : flow_table) {
    if (dest_ip >= rule.destIP_lo && dest_ip <= rule.destIP_hi) {
      rule.pktCount++;
      if (rule.actionType == "DROP") {
        break;
      } else if (rule.actionType == "FORWARD") {
        if (rule.actionVal != 3) {
          string relay_string = "RELAY:" + to_string(dest_ip);

          if (!port_to_fd.count(rule.actionVal)) {
            string relay_fifo = MakeFifoName(switch_id, rule.actionVal);
            int port_fd = OpenFifo(switch_id, port_to_id[rule.actionVal],
                                   O_WRONLY | O_NONBLOCK, relay_fifo);

            pair<int, int> port_conn =
                make_pair(port_to_id[rule.actionVal], port_fd);
            port_to_fd.insert(port_conn);
          }

          write(port_to_fd[rule.actionVal], relay_string.c_str(),
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
    int query_fd = port_to_fd[CONTROLLER_ID];
    string query_string = "QUERY:" + to_string(dest_ip);
    write(query_fd, query_string.c_str(), strlen(query_string.c_str()));
    if (errno) perror("Error: Failed to write.\n");
    errno = 0;
    query_count++;
  }
}

/**
 * Parses a line in the traffic file.
 */
tuple<int, int, int> ParseTrafficFileLine(string &line) {
  int id = 0;
  int src_ip = 0;
  int dest_ip = 0;

  istringstream iss(line);
  vector<string> tokens{istream_iterator<string>{iss},
                        istream_iterator<string>{}};
  if (tokens[0] == "#") {
    return make_tuple(-1, -1, -1);
  } else {
    id = ParseSwitchId(tokens[0]);

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

/**
 * List the current status of the switch.
 */
void SwitchList() {
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
  printf("\tReceived:    ADMIT:%i, ACK:%i, ADDRULE:%i, RELAYIN:%i\n",
         admit_count, ack_count, add_rule_count, relay_in_count);
  printf("\tTransmitted: OPEN: %i, QUERY:%i, RELAYOUT: %i\n", open_count,
         query_count, relay_out_count);
}

/**
 * Main event loop for the switch. Polls all input FIFOS.
 * Sends and receives packets of varying types.
 */
void SwitchLoop(int id, int port_1_id, int port_2_id, tuple<int, int> ip_range,
                ifstream &in) {
  // Add initial rule
  flow_rule initial_rule = {
      0, MAXIP, get<0>(ip_range), get<1>(ip_range), "FORWARD", 3, MINPRI, 0};
  flow_table.push_back(initial_rule);

  int pfd_index = 0;
  int receivers = 1;
  receivers = (port_1_id != -1) ? receivers + 1 : receivers;
  receivers = (port_2_id != -1) ? receivers + 1 : receivers;

  char buffer[MAX_BUFFER];
  struct pollfd pfds[receivers + 2];

  // Set up STDIN for polling from
  pfds[pfd_index].fd = STDIN_FILENO;
  pfds[pfd_index].events = POLLIN;
  pfds[pfd_index].revents = 0;
  pfd_index++;

  // Create and open a FIFO for reading from the controller
  int fd1 = CreateFifo(CONTROLLER_ID, id, O_RDONLY | O_NONBLOCK);
  pfds[pfd_index].fd = fd1;
  pfds[pfd_index].events = POLLIN;
  pfds[pfd_index].revents = 0;
  pfd_index++;

  // Open A FIFO for writing to the controller
  string write_fifo_name = MakeFifoName(id, CONTROLLER_ID);
  int fd2 = OpenFifo(id, CONTROLLER_ID, O_WRONLY | O_NONBLOCK, write_fifo_name);
  pair<int, int> cont_conn = make_pair(CONTROLLER_ID, fd2);
  port_to_fd.insert(cont_conn);

  // Send an OPEN packet to the controller
  string open_message = "OPEN:" + to_string(id) + "," + to_string(port_1_id) +
                        "," + to_string(port_2_id) + "," +
                        to_string(get<0>(ip_range)) + "," +
                        to_string(get<1>(ip_range));
  write(fd2, open_message.c_str(), strlen(open_message.c_str()));
  if (errno) perror("Error: Failed to write.\n");
  errno = 0;
  open_count++;

  // Create and open a reading FIFO for port 1 if not null
  if (port_1_id != -1) {
    pair<int, int> port_1_conn = make_pair(1, port_1_id);
    port_to_id.insert(port_1_conn);
    int port_1_fd = CreateFifo(port_1_id, id, O_RDONLY | O_NONBLOCK);
    pfds[pfd_index].fd = port_1_fd;
    pfds[pfd_index].events = POLLIN;
    pfds[pfd_index].revents = 0;
    pfd_index++;
  }

  // Create and open a reading FIFO for port 2 if not null
  if (port_2_id != -1) {
    pair<int, int> port_2_conn = make_pair(2, port_2_id);
    port_to_id.insert(port_2_conn);
    int port_2_fd = CreateFifo(port_2_id, id, O_RDONLY | O_NONBLOCK);
    pfds[pfd_index].fd = port_2_fd;
    pfds[pfd_index].events = POLLIN;
    pfds[pfd_index].revents = 0;
    pfd_index++;
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

  int signal_fd = pfd_index;
  pfds[pfd_index].fd = signalfd(-1, &sigset, 0);
  pfds[pfd_index].events = POLLIN;
  pfds[pfd_index].revents = 0;
  pfd_index++;

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
          HandlePacketUsingFlowTable(id, dest_ip);
        }
      } else {
        in.close();
      }
    }

    // Poll all input FIFOs.
    // Delayed slightly (100ms) to wait for response packets from the
    // controller.
    poll(pfds, receivers + 2, 100);
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
      if (!r) {
        printf("Warning: stdin closed.\n");
      }

      string cmd = string(buffer);
      Trim(cmd);  // Trim whitespace

      if (cmd == "list") {
        SwitchList();
      } else if (cmd == "exit") {
        SwitchList();
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

        if (packet_type == "ACK") {
          ack_count++;
        } else if (packet_type == "ADD") {
          flow_rule new_rule;
          string new_action;
          if (packet_message[0] == 0) {
            new_rule = {
                0,      MAXIP, packet_message[1], packet_message[2], "DROP", 0,
                MINPRI, 1};
          } else if (packet_message[0] == 1) {
            new_rule = {0,
                        MAXIP,
                        packet_message[1],
                        packet_message[2],
                        "FORWARD",
                        packet_message[3],
                        MINPRI,
                        1};

            // Relay the message based on the new rule
            string relay_string = "RELAY:" + to_string(packet_message[1]);

            if (!port_to_fd.count(packet_message[3])) {
              string relay_fifo = MakeFifoName(id, packet_message[3]);
              int port_fd = OpenFifo(id, port_to_id[packet_message[3]],
                                     O_WRONLY | O_NONBLOCK, relay_fifo);

              pair<int, int> port_conn =
                  make_pair(port_to_id[packet_message[3]], port_fd);
              port_to_fd.insert(port_conn);
            }

            write(port_to_fd[packet_message[3]], relay_string.c_str(),
                  strlen(relay_string.c_str()));
          } else {
            printf("Error: Invalid rule to add.\n");
            continue;
          }

          flow_table.push_back(new_rule);
          add_rule_count++;
        } else if (packet_type == "RELAY") {
          relay_in_count++;
          HandlePacketUsingFlowTable(id, packet_message[0]);
        } else {
          // Unknown packet. Used for debugging.
          printf("Received %s packet. Ignored.\n", packet_type.c_str());
        }
      }
    }

    /*
     * In addition, upon receiving signal USER1, the switch displays the information specified by the list command.
     */
    if (pfds[signal_fd].revents & POLLIN) {
        struct signalfd_siginfo info{};
        ssize_t r = read(pfds[signal_fd].fd, &info, sizeof(info));
        if (!r) {
            printf("Warning: Signal reading error.\n");
        }
        unsigned sig = info.ssi_signo;
        if (sig == SIGUSR1) {
            SwitchList();
        }
    }

    memset(buffer, 0, sizeof(buffer));  // Clear buffer
  }
}