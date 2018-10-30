#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <string.h>  // memset, strlen
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <algorithm>  // std::find_if
#include <fstream>    // std::fstream::in
#include <iostream>
#include <map>  // std::map
#include <sstream>
#include <string>  // std::string, std::to_string
#include <tuple>   // std::tuple
#include <vector>  // std::vector

using namespace std;

#define CONTROLLER_ID 0
#define MAX_NSW 7
#define MAXIP 1000
#define MINPRI 4
#define MAX_BUFFER 1024

typedef enum { OPEN, ACK, QUERY, ADD, RELAY } PACKET_TYPE;

typedef struct {
  PACKET_TYPE type;
  string message;
} packet;

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

string packet_to_string(packet &p) {
  return to_string(p.type) + ":" + p.message;
}

packet parse_packet_string(string &s) {
  string packet_type_token = s.substr(0, s.find(":"));
  PACKET_TYPE type =
      (PACKET_TYPE)strtol(packet_type_token.c_str(), (char **)NULL, 10);

  string packet_message_token = s.substr(s.find(":") + 1);

  return {type, packet_message_token};
}

vector<int> parse_open_message(string &m) {
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

  return vect;
}

// Trim from start (in place)
static inline void ltrim(string &s) {
  s.erase(s.begin(),
          find_if(s.begin(), s.end(), [](int ch) { return !isspace(ch); }));
}

// Trim from end (in place)
static inline void rtrim(string &s) {
  s.erase(
      find_if(s.rbegin(), s.rend(), [](int ch) { return !isspace(ch); }).base(),
      s.end());
}

// Trim from both ends (in place)
static inline void trim(string &s) {
  ltrim(s);
  rtrim(s);
}

string make_fifo_name(int sender_id, int receiver_id) {
  return "fifo-" + to_string(sender_id) + "-" + to_string(receiver_id);
}

// TODO: Perhaps error handling
int parse_switch_id(const string &input) {
  if (input == "null") {
    return -1;
  } else {
    string number = input.substr(2, input.length() - 1);
    return (int)strtol(number.c_str(), (char **)NULL, 10);
  }
}

tuple<int, int> process_ip_range(const string &input) {
  int ip_low;
  int ip_high;

  stringstream ss(input);
  string token;

  int i = 0;
  while (getline(ss, token, '-')) {
    if (token.length()) {
      if (i == 0) {
        ip_low = (int)strtol(token.c_str(), (char **)NULL, 10);
      } else if (i == 1) {
        ip_high = (int)strtol(token.c_str(), (char **)NULL, 10);
      }
      i++;
    } else {
      return make_tuple(-1, -1);
    }
  }

  return make_tuple(ip_low, ip_high);
}

void switch_list() { printf("List:\n"); }

void switch_loop(int id, int port_1_id, int port_2_id, tuple<int, int> ip_range,
                 vector<flow_rule> flow_table, ifstream &in) {
  vector<int> activeIds;
  activeIds.push_back(id);
  if (port_1_id != -1) activeIds.push_back(port_1_id);
  if (port_2_id != -1) activeIds.push_back(port_2_id);

  vector<connection> receive_connections;
  vector<connection> send_connections;

  char buffer[MAX_BUFFER];
  struct pollfd pfds[activeIds.size() + 1];
  pfds[0].fd = STDIN_FILENO;
  pfds[0].events = POLLIN;
  pfds[0].revents = 0;

  // TODO:

  // Send an OPEN packet to the controller
  string open_message = to_string(OPEN) + ":" + to_string(id) + "," +
                        to_string(port_1_id) + "," + to_string(port_2_id) +
                        "," + to_string(get<0>(ip_range)) + "," +
                        to_string(get<1>(ip_range));
  write(pfds[1].fd, open_message.c_str(), strlen(open_message.c_str()));
  if (errno) perror("Error: Failed to write.\n");
  errno = 0;

  while (true) {
    poll(pfds, activeIds.size() + 1, 0);
    if (errno) perror("Error: poll() failure.\n");
    errno = 0;

    if (pfds[0].revents & POLLIN) {
      ssize_t r = read(pfds[0].fd, buffer, MAX_BUFFER);
      if (!r) printf("Warning: stdin closed.\n");

      string cmd = string(buffer);
      trim(cmd);  // Trim whitespace

      if (cmd == "list") {
        switch_list();
      } else if (cmd == "exit") {
        switch_list();
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
    for (int i = 1; i <= (int)activeIds.size(); i++) {
      if (pfds[i].revents & POLLIN) {
        ssize_t r = read(pfds[i].fd, buffer, MAX_BUFFER);
        if (!r) {
          printf("Warning: Connection %s closed.\n",
                 receive_connections[i].fifo.c_str());
        }
        string packet_string = string(buffer);
        packet received_packet = parse_packet_string(packet_string);

        printf("Received packet: %s\n", buffer);

        if (received_packet.type == ACK) {
          // Ignore
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

void controller_list() { printf("List:\n"); }

void controller_loop(int num_switches) {
  // Generate receiver connections and FIFO names
  vector<connection> receive_connections;
  vector<connection> send_connections;

  struct pollfd pfds[num_switches + 1];
  pfds[0].fd = STDIN_FILENO;
  pfds[0].events = POLLIN;
  pfds[0].revents = 0;
  char buffer[MAX_BUFFER];

  for (int i = 1; i <= num_switches; i++) {
    string fifo_name = make_fifo_name(i, CONTROLLER_ID);
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

    send_connections.push_back({-1, ""});  // Placeholders
    receive_connections.push_back({fd, fifo_name});
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
      trim(cmd);  // Trim whitespace

      if (cmd == "list") {
        controller_list();
      } else if (cmd == "exit") {
        controller_list();
        exit(0);
      } else {
        printf("Error: Unrecognized command. Please use list or exit.\n");
      }
    }

    /*
     * 2. Poll the incoming FIFOs from the attached switches. The controller
     * handles each incoming packet, as described in the Packet Types section.
     */
    for (int i = 1; i < num_switches; i++) {
      if (pfds[i].revents & POLLIN) {
        ssize_t r = read(pfds[i].fd, buffer, MAX_BUFFER);
        if (!r) {
          printf("Warning: Connection %s closed.\n",
                 receive_connections[i].fifo.c_str());
        }
        string packet_string = string(buffer);
        packet received_packet = parse_packet_string(packet_string);

        printf("Received packet: %s\n", buffer);

        if (received_packet.type == OPEN) {
          // TODO: Make flow table rule
          vector<int> package_data =
              parse_open_message(received_packet.message);

          // Returns lowest unused file descriptor on success
          string fifo_name = make_fifo_name(CONTROLLER_ID, i);
          int fd = open(fifo_name.c_str(), O_WRONLY | O_NONBLOCK);
          if (errno) perror("Error: Could not open FIFO.\n");
          errno = 0;
          send_connections[i] = {fd, fifo_name};

          string ack_message = to_string(ACK) + ":";

          write(fd, ack_message.c_str(), strlen(ack_message.c_str()));
          if (errno) perror("Error: Could not write.\n");
          errno = 0;
        } else if (received_packet.type == QUERY) {
          // TODO: Reply with an ADD packet (message is the flow table rule)
          string add_message = to_string(ADD) + ":";

          write(send_connections[i].fd, add_message.c_str(),
                strlen(add_message.c_str()));
          if (errno) perror("Error: Could not write.\n");
          errno = 0;
        } else {
          printf("Received %s packet. Ignored.\n",
                 to_string(received_packet.type).c_str());
        }
      }
    }

    memset(buffer, 0, sizeof(buffer));  // Clear buffer
  }
}

/**
 * Main function. Processes command line arguments into inputs for either the
 * controller loop or the switch loop.
 */
int main(int argc, char **argv) {
  // Set a 10 minute CPU time limit
  rlimit time_limit{.rlim_cur = 600, .rlim_max = 600};
  setrlimit(RLIMIT_CPU, &time_limit);

  if (argc < 2) {
    printf("Too few arguments.\n");
    return 1;
  }

  string mode = argv[1];  // cont or swi
  if (mode == "cont") {
    if (argc != 3) {
      printf("Error: Invalid number of arguments. Expected 3.\n");
      return 1;
    }

    int num_switches = (int)strtol(argv[2], (char **)NULL, 10);
    if (num_switches > MAX_NSW || num_switches < 1) {
      printf("Error: Invalid number of switches. Must be 1-7.\n");
      return 1;
    }

    controller_loop(num_switches);
  } else if (mode.find("sw") != std::string::npos) {
    if (argc != 6) {
      printf("Error: Invalid number of arguments. Expected 6.\n");
      return 1;
    }

    int switch_id = parse_switch_id(argv[1]);
    if (switch_id == -1) {
      printf("Error: Switch ID is invalid.\n");
      return 1;
    }

    ifstream in(argv[2]);

    if (!in) {
      printf("Error: Cannot open file.\n");
      return 1;
    }

    int switchId1 = parse_switch_id(argv[3]);
    int switchId2 = parse_switch_id(argv[4]);

    tuple<int, int> ip_range = process_ip_range(argv[5]);

    if (get<0>(ip_range) == -1 && get<1>(ip_range) == -1) {
      printf("Error: Malformed IP range.\n");
      return 1;
    }

    vector<flow_rule> flow_table;
    flow_rule initial_rule = {
        0, MAXIP, get<0>(ip_range), get<1>(ip_range), "FORWARD", 3, MINPRI, 0};
    flow_table.push_back(initial_rule);

    switch_loop(switch_id, switchId1, switchId2, ip_range, flow_table, in);
  }

  return 0;
}
