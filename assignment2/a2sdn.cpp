#include <iostream>
#include <fstream>
#include <vector>
#include <tuple>
#include <string>
#include <sstream>
#include <sys/resource.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

using namespace std;

static const int CONTROLLER_ID = 0;
static const int MAX_NSW = 7;
static const int MAXIP = 1000;
static const int MINPRI = 4;
static const int MAX_BUFFER = 1024;

typedef enum {OPEN, ACK, QUERY, ADD, RELAY} PACKET_TYPE;

struct packet {
	PACKET_TYPE type;
	int id;
	int ip_low;
	int ip_high;
};

struct flow_rule {
	int srcIP_lo;
	int srcIP_hi;
	int destIP_lo;
	int destIP_hi;
	string actionType; // FORWARD, DROP
	int actionVal;
	int pri; // 0, 1, 2, 3, 4 (highest - lowest)
	int pktCount;
};

typedef enum {SEND, RECEIVE} MODE;

struct connection {
	MODE mode;
	string name;
};

string make_fifo_name(int sender_id, int receiver_id) {
    return "fifo-" + to_string(sender_id) + "-" + to_string(receiver_id);
}

vector<connection> make_connections(int switch_id, int port_1_switch_id, int port_2_switch_id) {
	vector<connection> output;

	output.push_back({SEND, make_fifo_name(switch_id, CONTROLLER_ID)});
	output.push_back({RECEIVE, make_fifo_name(CONTROLLER_ID, switch_id)});

	if (port_1_switch_id != 0) {
		output.push_back({SEND, make_fifo_name(switch_id, port_1_switch_id)});
		output.push_back({RECEIVE, make_fifo_name(port_1_switch_id, switch_id)});
	}
	
	if (port_2_switch_id != 0) {
		output.push_back({SEND, make_fifo_name(switch_id, port_2_switch_id)});
		output.push_back({RECEIVE, make_fifo_name(port_2_switch_id, switch_id)});
	}

	return output;
}

int parse_switch_id(const string &input) {
	if (input == "null") {
		return 0;
	} else {
		string number = input.substr(2, input.length() - 1);
		if (number == "") {
			return -1;
		}
		return (int) strtol(number.c_str(), (char **) NULL, 10);
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
                ip_low = (int) strtol(token.c_str(), (char **) NULL, 10);
            } else if (i == 1) {
                ip_high = (int) strtol(token.c_str(), (char **) NULL, 10);
            }
            i++;
        } else {
			return make_tuple(-1, -1);
		}
    }

    return make_tuple(ip_low, ip_high);
}

void switch_loop(int id, tuple<int, int> ip_range, vector<flow_rule> flow_table, vector<connection> connections, ifstream &in) {
	char buffer[MAX_BUFFER];
    struct pollfd pfds[2 * connections.size()]; // Both way connections

	// Create and open FIFOs for all connections
	int i;
	for (auto &this_connection : connections) {
		int status = mkfifo(this_connection.name.c_str(), S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    	if (errno || status == -1) {
			errno = 0;
			printf("FIFO: %s\n", this_connection.name.c_str());
        	perror("Error: Could not create a FIFO connection.\n");
			continue;
    	}

		int rw_flag = (this_connection.mode == MODE::SEND) ? O_WRONLY : O_RDONLY;

		// Returns lowest unused file descriptor on success
		if (this_connection.mode == MODE::RECEIVE) {
			int fd = open(this_connection.name.c_str(), rw_flag | O_NONBLOCK);
			if (errno || fd == -1) {
				errno = 0;
				printf("FIFO: %s\n", this_connection.name.c_str());
				perror("Error: Could not open FIFO.\n");
			}

			pfds[i].fd = fd;
		}

		i++;
	}

	// Send an OPEN packet to the controller
	packet open_packet = {OPEN, id, get<0>(ip_range), get<1>(ip_range)};
	write(pfds[0].fd, &open_packet, sizeof(struct packet));

	// TODO: Read file properly. This is just a test.
	string str;
	while (getline(in, str)) {
		cout << str << endl;
	}
}

void controller_loop() {}

int main(int argc, char **argv) {
	// Set a 10 minute CPU time limit
	rlimit time_limit {
        .rlim_cur = 600,
        .rlim_max = 600
    };
    setrlimit(RLIMIT_CPU, &time_limit);
	
    if (argc < 3) {
        printf("Too few arguments.\n");
        return 1;
    }
	
	string mode = argv[1]; // cont or swi
	if (mode == "cont") {
		int num_switches = (int) strtol(argv[2], (char **) NULL, 10);
		if (num_switches > MAX_NSW || num_switches < 1) {
			printf("Error: Invalid number of switches. Must be 1-7.\n");
			return 1;
		}
		controller_loop();
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
		if (switchId1 == -1) {
			printf("Error: Switch ID is invalid.\n");
			return 1;
		}

		int switchId2 = parse_switch_id(argv[4]);
		if (switchId2 == -1) {
			printf("Error: Switch ID is invalid.\n");
			return 1;
		}
		
		tuple<int, int> ip_range = process_ip_range(argv[5]);
		
		if (get<0>(ip_range) == -1 && get<1>(ip_range) == -1) {
			printf("Error: Malformed IP range.\n");
			return 1;
		}

		vector<flow_rule> flow_table;
		flow_rule initial_rule = {0, MAXIP,	get<0>(ip_range), get<1>(ip_range),	"FORWARD", 3, MINPRI, 0};
		flow_table.push_back(initial_rule);

		vector<connection> connections = make_connections(switch_id, switchId1, switchId2);

		switch_loop(switch_id, ip_range, flow_table, connections, in);
	}

    return 0;
}
