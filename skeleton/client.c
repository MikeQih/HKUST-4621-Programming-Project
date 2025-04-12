#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <strings.h>
#include <sys/types.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <poll.h>

#define SERVER "127.0.0.1"
#define SERVER_PORT 5000

#define MAXMSG 1400
#define MAXNAME 20
#define OP_SIZE 20

#define SEQ0 0
#define SEQ1 1

#define YES "Y"
#define FILE_FLAG 1
#define CHAT_FLAG 2

#define REGISTER "REGISTER"
#define BROADCAST "@ALL:"
#define WHO "WHO"
#define AT "@"
#define UPDATE "UPDATE"
#define QUERY "QUERY"
#define RESPONSE "RESPONSE"
#define FINISH "FINISH"
#define ACK "ACK"
#define GET "GET"
#define EXIT "EXIT"

#define SYSINFO "SYS@ALL:"
#define WELCOME "WELCOME"
#define GOODBYE "GOODBYE"

#define TIMEOUT 500000		/* 1000 ms */



/* This structure can be used to pass arguments */
struct ip_port {
	unsigned int ip;
	unsigned short port;
};

// Get a line until '\n'
void get_a_line(char* buffer, int buf_size) {
	char c;
	int i = 0;
	c = getchar();
	if (c != '\n')
		buffer[i++] = c;
	while ((c = getchar()) != '\n' && i + 1 < buf_size) {
		if (c == EOF)
			while(1);
        buffer[i++] = c;
    }
	buffer[i] = '\0';
}

int set_timeout(int sockfd, int usec) {
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = usec; /* 100 ms */
	int ret = setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO,
			(struct timeval *)&tv, sizeof(struct timeval));
	if (ret == SO_ERROR) {
		return -1;
	}
	return 0;
}


int unset_timeout(int sockfd) {
	return set_timeout(sockfd, 0);
}


int rdt3_send(int sockfd, struct sockaddr_in servaddr, char ack_num, char *buffer, unsigned len) {
	int waiting = 1;
	char noack_num = ack_num;
	struct sockaddr_in recv_addr;
	unsigned int recv_len;
	char recv_buf[MAXMSG];

	char seq;
	char op[OP_SIZE];
	char remain[MAXMSG];

	sendto(sockfd, (const char *)buffer, len,
		0, (const struct sockaddr *) &servaddr, sizeof(servaddr));

	set_timeout(sockfd, TIMEOUT);

	int parse_idx = 0;
	char return_code = 0; // only used for register

	while (waiting) {
		/***********************************************
		 * You should receive and parse the response here.
		 * Then, you should check whether it is the ACK
		 * packet you are waiting for by comparing the
		 * sequence number and checking packet type.
		 * Waiting until receiving the right ACK.
		 *
		 * START YOUR CODE HERE
		 **********************************************/



		/***********************************************
		 * END OF YOUR CODE
		 **********************************************/
		bzero(recv_buf, MAXMSG);
	}

	unset_timeout(sockfd);

	return (int)return_code;
}


/* Send query to the server */
int send_register(int sockfd, struct sockaddr_in servaddr, unsigned int ip, unsigned short port, const char* name, char register_flag) {
	char buffer[MAXMSG];
	
	bzero(buffer, MAXMSG);

	char seq = SEQ1;

	/* Compose send buffer: REGISTER ip port name register_flag */
	int total_len = 0;

	memcpy(buffer, &seq, sizeof(seq));
	total_len ++; /* add a seq */

	buffer[total_len] = ' ';
	total_len ++; /* add a blank */

	memcpy(buffer + total_len, REGISTER, strlen(REGISTER));
	total_len += strlen(REGISTER);

	buffer[total_len] = ' ';
	total_len ++; /* add a blank */

	memcpy(buffer + total_len, &ip, sizeof(ip));
	total_len += sizeof(ip);

	memcpy(buffer + total_len, &port, sizeof(port));
	total_len += sizeof(port);

	memcpy(buffer + total_len, name, MAXNAME);
	total_len += MAXNAME;

	memcpy(buffer + total_len, &register_flag, sizeof(register_flag));
	total_len += sizeof(register_flag);

	buffer[total_len] = '\0';
	int return_code = rdt3_send(sockfd, servaddr, seq, buffer, total_len);

	if (return_code == 0)
		printf("REGISTER finished!\n");
	else if (return_code == 1)
		printf("REGISTER failed: (%u, %hu) already exists!\n", ip, port);
	else if (return_code == 2)
		printf("REGISTER failed: '%s' already exists!\n", name);
	else
		printf("REGISTER failed: unknown mistake!\n");
	return return_code;
}


/* Send query to the server */
int send_update(int sockfd, struct sockaddr_in servaddr, unsigned int ip, unsigned short port, unsigned file_map, char updated_flag) {
	char buffer[MAXMSG];
	
	bzero(buffer, MAXMSG);

	char seq = SEQ1;

	/***********************************************
	 * You should follw the send_register pattern to
	 * implement a function to send UPDATE message.
	 *
	 * START YOUR CODE HERE
	 **********************************************/



	/***********************************************
	 * END OF YOUR CODE
	 **********************************************/
	
	printf("UPDATE finished !\n");

	return 0;
}


int receive_query(int sockfd, struct sockaddr_in servaddr) {
	struct sockaddr_in recv_addr;
	unsigned int recv_len;
	char buffer[MAXMSG];

	int n = 0;
	unsigned parse_idx = 0;
	char seq;

	char send_buf[MAXMSG];
	unsigned send_idx = 0;

	unset_timeout(sockfd);

	printf("Receiving query ...\n");

	char unfinished = 1;
	while (unfinished) {

		n = recvfrom(sockfd, (char *)buffer, MAXMSG,
				MSG_WAITALL, ( struct sockaddr *) &recv_addr, &recv_len);
		if (n < 0) {
			continue;
		}

		buffer[n] = '\0';

		seq = buffer[0];
		parse_idx += 2; /* skip seq and blank */

		/* If receive FINISH signal, stop receiving. */
		if (strncmp(buffer + parse_idx, FINISH, strlen(FINISH)) == 0) {
			unfinished = 0;

		/* Receive RESPONSE */
		} else if (strncmp(buffer + parse_idx, RESPONSE, strlen(RESPONSE)) == 0) {
			// Receive and parse packet
			unsigned int ip;
			unsigned short port;
			char name[MAXNAME];
			bzero(name, sizeof(name));

			parse_idx += strlen(RESPONSE);
			parse_idx ++; /*skip blank */

			memcpy(&ip, buffer + parse_idx, sizeof(ip));
			parse_idx += sizeof(ip);

			memcpy(&port, buffer + parse_idx, sizeof(port));
			parse_idx += sizeof(port);

			memcpy(name, buffer + parse_idx, sizeof(name));
			parse_idx += sizeof(name);

			struct in_addr addr;
			addr.s_addr = ip;
			char *ip_str = inet_ntoa(addr);

			printf("'%s' @ %s : %d\n", name, ip_str, port);

		} else {
			printf("Unknown operation: %s", buffer + 2);
			goto nooperation;
		}

		// Compose and send ACK packet
		memcpy(send_buf, &seq, sizeof(seq));
		send_idx += 2; /* seq and blank */

		memcpy(send_buf + send_idx, ACK, strlen(ACK));
		send_idx += strlen(ACK);

		int res = sendto(sockfd, (const char *)send_buf, send_idx,
				0, (const struct sockaddr *) &servaddr, sizeof(servaddr));

	nooperation:
		bzero(buffer, MAXMSG);
		bzero(send_buf, MAXMSG);
		parse_idx = 0;
		send_idx = 0;
	}

	return 0;
}


/* Send query to the server */
int send_query(int sockfd, struct sockaddr_in servaddr, char *filename, int len) {
	char buffer[MAXMSG];

	bzero(buffer, sizeof(buffer));

	char seq = SEQ1;

	/* Compose send buffer: REGISTER IP Port */
	int total_len = 0;

	memcpy(buffer, &seq, sizeof(seq));
	total_len ++; /* add a seq */

	buffer[total_len] = ' ';
	total_len ++; /* add a blank */

	memcpy(buffer + total_len, QUERY, strlen(QUERY));
	total_len += strlen(QUERY);

	buffer[total_len] = ' ';
	total_len ++; /* add a blank */

	memcpy(buffer + total_len, filename, len);
	total_len += len;

	buffer[total_len] = '\0';

	rdt3_send(sockfd, servaddr, seq, buffer, total_len);

	printf("QUERY finished !\n");

	/*sleep(1); [> begin to receive queried messages <]*/
	
	receive_query(sockfd, servaddr);

	return 0;
}

unsigned int get_file_map() {
	DIR *dir;
    struct dirent *entry;
	unsigned int file_map = 0U;

    // Open the current directory
    dir = opendir(".");
    if (dir == NULL) {
        perror("opendir");
        return 1;
    }

    // Enumerate files in the directory
    while ((entry = readdir(dir)) != NULL) {
		unsigned int bit = (1U << 31);

		char file_idx = (entry->d_name[0] - '0') * 10 + (entry->d_name[1] - '0');
		if (file_idx < 0 || file_idx > 31) {
			continue;
		}

		file_map |= (bit >> file_idx);
    }

    // Close the directory
    closedir(dir);
	return file_map;
}


// Add a new file descriptor to the set
void add_to_pfds(struct pollfd *pfds[], int newfd, int *fd_count, int *fd_size)
{
    // If we don't have room, add more space in the pfds array
    if (*fd_count == *fd_size) {
        *fd_size *= 2; // Double it

        *pfds = realloc(*pfds, sizeof(**pfds) * (*fd_size));
    }

    (*pfds)[*fd_count].fd = newfd;
    (*pfds)[*fd_count].events = POLLIN; // Check ready-to-read

    (*fd_count)++;
}

// Remove an index from the set
void del_from_pfds(struct pollfd pfds[], int i, int *fd_count)
{
    // Copy the one from the end over this one
    pfds[i] = pfds[*fd_count-1];

    (*fd_count)--;
}


void* p2p_server(void* arg) {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[MAXMSG] = {0};
    FILE *fp;
    ssize_t bytes_read;

	struct ip_port *ip_port_info = (struct ip_port *) arg;

	int fd_count = 0;
	int fd_size = 100; /* at most 100 sockets */
	struct pollfd *pfds = malloc(sizeof *pfds * fd_size);

    // Create socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Set socket options
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt error");
        exit(EXIT_FAILURE);
    }

    // Bind socket to address and port
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(ip_port_info->port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_fd, 10) < 0) {
        perror("listen fails to start");
        exit(EXIT_FAILURE);
    }

	printf("p2p server is listening ...\n");

	/***********************************************
	 * Initialize the pfds here.
	 *
	 * START YOUR CODE HERE
	 **********************************************/



	/***********************************************
	 * END OF YOUR CODE
	 **********************************************/

	while (1) {
		int poll_count = poll(pfds, fd_count, -1);

		if (poll_count == -1) {
			perror("poll error");
			exit(1);
		}

		// Run through the existing connections looking for data to read
		for(int i = 0; i < fd_count; i++) {
			// Check if someone's ready to read
			if (pfds[i].revents & POLLIN) {
				if (pfds[i].fd == server_fd) { /* the server receives a connection request */
					/***********************************************
					 * Add your code here to receive a new connection.
					 *
					 * START YOUR CODE HERE
					 **********************************************/



					/***********************************************
					 * END OF YOUR CODE
					 **********************************************/

				} else { /* the client receive a message */

					new_socket = pfds[i].fd;

					// Receive file name from client
					if (recv(new_socket, buffer, MAXMSG, 0) < 0) {
						perror("recv");
						exit(EXIT_FAILURE);
					}

					// Open requested file
					fp = fopen(buffer, "rb");
					if (fp == NULL) {
						perror("fopen file error");
						exit(EXIT_FAILURE);
					}

					bzero(buffer, MAXMSG);

					long file_size;
					// Get the file size
					fseek(fp, 0, SEEK_END);
					file_size = ftell(fp);
					fseek(fp, 0, SEEK_SET);

					/***********************************************
					 * Refer to the description, send the file length
					 * and file content to the p2p client.
					 *
					 * START YOUR CODE HERE
					 **********************************************/



					/***********************************************
					 * END OF YOUR CODE
					 **********************************************/

					fclose(fp);
					close(new_socket);
					del_from_pfds(pfds, i, &fd_count);

					bzero(buffer, MAXMSG);

				}
			}
		}
	}

    close(server_fd);
    return NULL;
}


int p2p_client(unsigned int ip, unsigned short port, char *file_name) {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[MAXMSG] = {0};
    FILE *fp;
    ssize_t bytes_read;

    // Create socket file descriptor
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket fails to create");
        return -1;
    }

    // Set server address and port
    serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = ip;
    serv_addr.sin_port = htons(port);

	printf("Connecting to p2p server ...\n");

    // Connect to server
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect error");
        return -1;
    }

	sleep(5);

    // Send file name to server
    if (send(sock, file_name, strlen(file_name), 0) < 0) {
        perror("send file name error");
        return -1;
    }

    // Receive file contents from server
    fp = fopen(file_name, "wb");
    if (fp == NULL) {
        perror("fopen file error");
        return -1;
    }

	/***********************************************
	 * Refer to the description of file transfer
	 * process to receive and save the file.
	 *
	 * START YOUR CODE HERE
	 **********************************************/



	/***********************************************
	 * END OF YOUR CODE
	 **********************************************/

	printf("Receive finished !\n");

    fclose(fp);
    close(sock);
    return 0;
}


// Routine for receving messages when chatting
void* chat_recv(void* args) {
	int chat_sockfd = *((int*)args);
	char buffer[MAXMSG];
	while (1)
	{
		bzero(buffer, sizeof(buffer));
		if (recv(chat_sockfd, buffer, sizeof(buffer), 0) < 0) {
			perror("recv");
			return NULL;
		}
		printf("%s\n", buffer);
	}
}

void cmd_menu() {
	printf("The valid commands are as follow:\n\n");
	printf("1. Register for file sharing, chatting, or both by:\n");
	printf("REGISTER name file_flag(Y/N) chat_flag(Y/N)\n\n");
	printf("2. Broadcast message to everyone in chatting by:\n");
	printf("@ALL:message\n\n");
	printf("3. Get a list of users in chatting by:\n");
	printf("WHO\n\n");
	printf("4. Send message to the user with the name by:\n");
	printf("@name:message\n\n");
	printf("5. Update state for file sharing, chatting, or both by:\n");
	printf("UPDATE file_flag(Y/N) chat_flag(Y/N)\n\n");
	printf("6. Query for a file called from 00.txt to 31.txt by:\n");
	printf("QUERY xx.txt\n\n");
	printf("7. Get the file queried from a user at (ip, port) by:\n");
	printf("GET ip port\n\n");
	printf("8. Exit from the system by Ctrl + C or:\n");
	printf("EXIT\n\n");
}

int main() {
	char ip_string[20] = "127.0.0.1";
	unsigned short port = 6000;

	unsigned int ip = inet_addr(ip_string);

	printf("\nInput port number: ");
	scanf("%hu", &port);

	/* start p2p server service */
	pthread_t tid;
	struct ip_port arg;
	arg.ip = ip;
	arg.port = port;

	printf("Creating p2p server ...\n");

    /***********************************************
	 * Start the p2p server using pthread.
	 *
	 * START YOUR CODE HERE
	 **********************************************/



	/***********************************************
	 * END OF YOUR CODE
	 **********************************************/

	sleep(1);

	int sockfd;
	struct sockaddr_in servaddr;

	// Creating socket file descriptor
	if ( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
		perror("socket creation failed");
		exit(EXIT_FAILURE);
	}

	// TCP client socket for chatting
	int chat_sockfd;
	if ((chat_sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("chat socket creation failed");
		exit(EXIT_FAILURE);
	}

	memset(&servaddr, 0, sizeof(servaddr));
	
	// Filling server information
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = inet_addr(SERVER);
	servaddr.sin_port = htons(SERVER_PORT);

	char command[MAXMSG];
	char file_name[256];
	unsigned int local_map;

	char register_flag = 0;
	char query_flag = 0;

	cmd_menu();
	do {
		bzero(command, sizeof(command));

		get_a_line(command, sizeof(command));

		// cmd: REGISTER name file_flag(Y/N) chat_flag(Y/N)
		if (strncmp(command, REGISTER, strlen(REGISTER)) == 0) {
			// parse name
			char name_buffer[MAXMSG];
			bzero(name_buffer, sizeof(name_buffer));
			int parse_idx = strlen(REGISTER) + 1;
			while (command[parse_idx] != ' ')
				parse_idx++;
			memcpy(name_buffer, command + strlen(REGISTER) + 1, parse_idx - strlen(REGISTER) - 1);
			char name[MAXNAME];
			bzero(name, sizeof(name));
			memcpy(name, name_buffer, sizeof(name));
			// parse two flags
			char send_register_flag = 0;
			parse_idx++; // skip blank
			if (strncmp(command + parse_idx, YES, strlen(YES)) == 0) {
				send_register_flag |= FILE_FLAG;
			}
			parse_idx += strlen(YES);
			parse_idx++; // skip blank
			if (strncmp(command + parse_idx, YES, strlen(YES)) == 0) {
				send_register_flag |= CHAT_FLAG;
			}
			if (send_register_flag == 0) {
				printf("You need to enable one of file sharing and chatting at least!\n");
				continue;
			}
			if (send_register(sockfd, servaddr, ip, port, name, send_register_flag) == 0) {
				if (connect(chat_sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) != 0) {
					perror("connection with server failed");
					exit(EXIT_FAILURE);
				}
				
				pthread_t chat_recv_thread;
				if (pthread_create(&chat_recv_thread, NULL, chat_recv, (void*)&chat_sockfd) != 0) {
					perror("pthread_create");
					exit(EXIT_FAILURE);
				}
				register_flag = send_register_flag;
				
				// for file sharing flag
				if (register_flag & FILE_FLAG) {
					local_map = get_file_map();
					send_update(sockfd, servaddr, ip, port, local_map, register_flag); // the same flag is passed, only update file map
				}
				// for chatting flag
				if (register_flag & CHAT_FLAG) {
					char msg[MAXMSG];
					bzero(msg, sizeof(msg));
					strcat(msg, SYSINFO);
					strcat(msg, WELCOME);
					if (send(chat_sockfd, msg, sizeof(msg), 0) < 0) {
						perror("Sysinfo failed");
					}
				}
			}
			continue;
		}
		// cmd: @ALL:message
		if (strncmp(command, BROADCAST, strlen(BROADCAST)) == 0) {
			if (!(register_flag & CHAT_FLAG)) {
				if (register_flag == 0)
					printf("You should first register to enable chatting!\n");
				else
					printf("You should update to enable chatting!\n");
				continue;
			}
			if (send(chat_sockfd, command, sizeof(command), 0) < 0) {
				perror("Broadcast failed");
			}
			continue;
		}
		// cmd: WHO
		if (strncmp(command, WHO, strlen(WHO)) == 0) {
			if (!(register_flag & CHAT_FLAG)) {
				if (register_flag == 0)
					printf("You should first register to enable chatting!\n");
				else
					printf("You should update to enable chatting!\n");
				continue;
			}
			if (send(chat_sockfd, command, sizeof(command), 0) < 0) {
				perror("WHO failed");
			}
			continue;
		}
		// cmd: @name:message
		if (strncmp(command, AT, strlen(AT)) == 0) {
			if (!(register_flag & CHAT_FLAG)) {
				if (register_flag == 0)
					printf("You should first register to enable chatting!\n");
				else
					printf("You should update to enable chatting!\n");
				continue;
			}
			char buffer[MAXMSG];
			bzero(buffer, sizeof(buffer));
			char name[MAXNAME];
			bzero(name, sizeof(name));
			int parse_idx = strlen(AT);
			while (command[parse_idx] != ':')
				parse_idx++;
			memcpy(name, command + strlen(AT), parse_idx - strlen(AT));
			char msg[MAXMSG];
			bzero(msg, sizeof(msg));
			memcpy(msg, command + parse_idx + 1, MAXMSG - parse_idx - 1);
			memcpy(buffer, AT, sizeof(AT));
			memcpy(buffer + sizeof(AT), name, sizeof(name));
			memcpy(buffer + sizeof(AT) + sizeof(name), msg, sizeof(msg));
			if (send(chat_sockfd, buffer, sizeof(buffer), 0) < 0) {
				perror("AT failed");
			}
			continue;
		}
		// cmd: UPDATE file_flag(Y/N) chat_flag(Y/N)
		if (strncmp(command, UPDATE, strlen(UPDATE)) == 0) {
			if ( register_flag == 0 ) {
				printf("You should first register!\n");
			} else {
				int parse_idx = strlen(UPDATE);
				// parse two flags
				char updated_flag = 0;
				parse_idx++; // skip blank
				if (strncmp(command + parse_idx, YES, strlen(YES)) == 0) {
					updated_flag |= FILE_FLAG;
				}
				parse_idx += strlen(YES);
				parse_idx++; // skip blank
				if (strncmp(command + parse_idx, YES, strlen(YES)) == 0) {
					updated_flag |= CHAT_FLAG;
				}
				if (updated_flag == 0) {
					printf("You need to enable one of file sharing and chatting at least!\n");
					continue;
				}
				// for file sharing flag
				if (updated_flag & FILE_FLAG)
					local_map = get_file_map();
				// update flag first then welcome or goodbye system information can be enabled
				send_update(sockfd, servaddr, ip, port, local_map, updated_flag);
				// for chatting flag
				if (((updated_flag & CHAT_FLAG) && !(register_flag & CHAT_FLAG))
					|| (!(updated_flag & CHAT_FLAG) && (register_flag & CHAT_FLAG))) {
					char msg[MAXMSG];
					bzero(msg, sizeof(msg));
					strcat(msg, SYSINFO);
					if (updated_flag & CHAT_FLAG)
						strcat(msg, WELCOME);
					else
						strcat(msg, GOODBYE);
					if (send(chat_sockfd, msg, sizeof(msg), 0) < 0) {
						perror("Sysinfo failed");
					}
				}
				register_flag = updated_flag;
			}
			continue;
		}
		// cmd: QUERY xx.txt
		if (strncmp(command, QUERY, strlen(QUERY)) == 0) {
			if (!(register_flag & FILE_FLAG)) {
				printf("You should first register to enable file sharing!\n");
				continue;
			}
			strcpy(file_name, command + strlen(QUERY) + 1);
			send_query(sockfd, servaddr, file_name, strlen(file_name));
			query_flag = 1;
			continue;
		}
		// cmd: GET ip port
		if (strncmp(command, GET, strlen(GET)) == 0) {
			if (!(register_flag & FILE_FLAG)) {
				printf("You should first register to enable file sharing!\n");
				continue;
			}
			if ( query_flag == 0 ) {
				printf("You should first query a file!\n");
			} else {
				query_flag = 0;
				char input_ip[32];
				char input_port_s[32];
				unsigned short input_port;
				int parse_idx = strlen(GET) + 1;
				while (command[parse_idx] != ' ')
					parse_idx++;
				memcpy(input_ip, command + strlen(GET) + 1, parse_idx - strlen(GET) - 1);
				strcpy(input_port_s, command + parse_idx + 1);
				input_port = atoi(input_port_s);

				p2p_client(inet_addr(input_ip), input_port, file_name);

				sleep(2);

				/* then update */
				local_map = get_file_map();
				send_update(sockfd, servaddr, ip, port, local_map, register_flag); // the same flag is passed, only update file map
			}

			continue;
		}
		// cmd: EXIT
		if (strncmp(command, EXIT, strlen(EXIT)) == 0) {
			printf("Exit!\n");
			return 0;
		}

		printf("Invalid command is provided.\n");
		cmd_menu();

	} while (1);

    // Wait for server thread to finish
    if (pthread_join(tid, NULL) != 0) {
        perror("pthread_join");
        return 1;
    }

	close(sockfd);
	return 0;
}

