#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <poll.h>

#define SERVER "127.0.0.1"
#define SERVER_PORT 5000
// #define SERVER_PORT 8080

#define MAXMSG 1400
#define MAXNAME 20
#define OP_SIZE 20

#define SEQ0 0
#define SEQ1 1

#define FILE_FLAG 1
#define CHAT_FLAG 2

#define REGISTER "REGISTER"
#define WELCOME "WELCOME"
#define BROADCAST "@ALL:"
#define WHO "WHO"
#define AT "@"
#define UPDATE "UPDATE"
#define QUERY "QUERY"
#define RESPONSE "RESPONSE"
#define FINISH "FINISH"
#define ACK "ACK"

#define SYSINFO "SYS@ALL:"
#define WELCOME "WELCOME"
#define GOODBYE "GOODBYE"

#define TIMEOUT 500000		/* 1000 ms */



/* This structure can be used to parse packet */
struct ip_port {
	unsigned int ip;
	unsigned short port;
};


struct node{
	unsigned int ip;		/* ip and port are used as index */
	unsigned short port;    /* the port number of TCP listening */
	unsigned int file_map;	/* a bitmap for files */
	char name[MAXNAME];		/* a unique username */
	char register_flag;	/* 1: file sharing, 2: chatting, 3: both */
	struct node *next;
};


struct rdt3_sender_ctx{
	unsigned int ip;			/* ip and port are used as index */
	unsigned short port;		/* the port number of current socket */
	char waiting_ack;			/* if waiting ack */
	char noack_num;				/* waiting ack num */
	char file_idx;				/* file index for QUERY & RESPONSE */
	long long clock;			/* the clock time of last packet sending, for timeout */
	struct node *noack_node;	/* waiting ack node, maybe retransmitted */
	/*struct node *node_info;		[> related node info <]*/
	struct rdt3_sender_ctx *next;
};

// let chatting thread can access the two lists
struct args {
	struct node** node_head;
	struct rdt3_sender_ctx** ctx_head;
};


long long get_current_time() {
    struct timeval current_time;

    // Get the current time with microsecond precision
    if (gettimeofday(&current_time, NULL) == -1) {
        perror("gettimeofday");
        return 1;
    }

    // Calculate the total milliseconds
    long long total_microseconds = current_time.tv_sec * 1000000LL + current_time.tv_usec;
	return total_microseconds;
}


int set_timeout(int sockfd, int usec) {
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = usec;
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

// Initialize an empty linked list
struct node* init_node_list() {
    return NULL; // Return NULL indicating an empty list
}

// Insert a new node at the end of the linked list
void insert_node(struct node **head, unsigned ip, unsigned short port, unsigned file_map, const char* name, char register_flag) {
    // Create a new node
    struct node* newnode = (struct node*)malloc(sizeof(struct node));
    if (newnode == NULL) {
        printf("Memory allocation failed.\n");
        return;
    }

	newnode->ip = ip;
	newnode->port = port;
	newnode->file_map = file_map;
	memcpy(newnode->name, name, MAXNAME);
	newnode->register_flag = register_flag;
    newnode->next = NULL;

    // If the list is empty, set the new node as the head
    if (*head == NULL) {
        *head = newnode;
        return;
    }

    // Traverse the list to find the last node
    struct node* current = *head;
    while (current->next != NULL) {
        current = current->next;
    }

    current->next = newnode;
}

// Query a node in the linked list
struct node* query_node(struct node *head, unsigned ip, unsigned short port, const char* name) {
    // Check if the list is empty
    if (head == NULL) {
        return NULL;
    }

    struct node* current = head;

    while (current != NULL && (current->ip != ip || current->port != port) && (name == NULL || strcmp(current->name, name) != 0)) {
        current = current->next;
    }

    // If the index is out of bounds, return NULL
    if (current == NULL) {
        return NULL;
    }

    return current;
}

// Traverse the linked list to collect names in chatting except the node at index
void collect_names_in_chatting(struct node *head, int index, char* names) {
	/***********************************************
	 * Traverse the linked list of nodes and collect
	 * names in chat except the node at index.
	 *
	 * START YOUR CODE HERE
	 **********************************************/
	struct node *current = head;
	int idx = 0;
	while (current != NULL) {
        // 如果节点启用了聊天功能，并且不是当前用户
        if ((current->register_flag & CHAT_FLAG) && idx != index) {
            strcat(names, "'");
            strcat(names, current->name);
            strcat(names, "' ");
			/*
			对于每个节点，如果用户注册时启用了聊天功能（通过 CHAT_FLAG 标记）且该节点的索引不等于传入的 index（即排除当前用户），
			则将这个用户的名字追加到字符串 names 中，并用单引号括起来，名字之间用空格分隔。
			*/
        }
        current = current->next;
        idx++;
    }


	/***********************************************
	 * END OF YOUR CODE
	 **********************************************/
}

// Query index and flag by name in the linked list
int query_idx_flag_by_name(struct node *head, const char* name, int* index, char* flag) {
	/***********************************************
	 * Query the index and flag of a node with name
	 * in the linked list. Return 0 on success and
	 * -1 otherwise.
	 *
	 * START YOUR CODE HERE
	 **********************************************/
	struct node *current = head;
    int idx = 0;
	while (current != NULL)
	{
		if (strcmp(current->name,name) == 0)
		{
			*index = idx;
			*flag = current->register_flag;
			return 0;
		}
		current = current->next;
		idx++;
	}
	return -1;
	
	/***********************************************
	 * END OF YOUR CODE
	 **********************************************/
}

// Query name and flag by index in the linked list
int query_name_flag_by_idx(struct node *head, int index, char* name, char* flag) {
    /***********************************************
	 * Query the name and flag of a node at index
	 * in the linked list. Return 0 on success and
	 * -1 otherwise.
	 *
	 * START YOUR CODE HERE
	 **********************************************/
	struct node *current = head;
    int idx = 0;
	while (current != NULL && idx<index)
	{
		current = current -> next;
		idx++;
	}

	if (current == NULL)
	{
		return -1;
	}
	
	strcpy(name, current->name); //把 current->name 里的字符串 复制到 name 这个变量中。
    *flag = current->register_flag;
    return 0; // 成功

	/***********************************************
	 * END OF YOUR CODE
	 **********************************************/
}

// Remove a node by index
void remove_node(struct node** head, int index) {
    // Check if the list is empty
    if (*head == NULL) {
        return;
    }

	if (index == 0) { //index is 头节点
		struct node* next = (*head)->next;
		free(*head);
		*head = next;
		return;
	}

    struct node* prev = *head; //定义一个指针 prev 指向当前头节点，准备遍历链表找目标节点。
	struct node* curr = (*head)->next;
	int idx = 1;
    while (curr != NULL) {
		if (idx == index) {
			struct node* next = curr->next;
			free(curr);
			prev->next = next;
			return;
		}
		prev = curr;
        curr = curr->next;
		idx += 1;
    }
}

// Initialize an empty linked list
struct rdt3_sender_ctx* init_rdt3_sender_ctx_list() {
    return NULL; // Return NULL indicating an empty list
}

// Insert a new rdt3_sender_ctx at the end of the linked list
void insert_ctx(struct rdt3_sender_ctx **head, unsigned ip, unsigned short port) {
    // Create a new rdt3_sender_ctx
    struct rdt3_sender_ctx* newrdt3_sender_ctx = (struct rdt3_sender_ctx*)malloc(sizeof(struct rdt3_sender_ctx));
    if (newrdt3_sender_ctx == NULL) {
        printf("Memory allocation failed.\n");
        return;
    }

	newrdt3_sender_ctx->ip = ip;
	newrdt3_sender_ctx->port = port;
    newrdt3_sender_ctx->next = NULL;

    // If the list is empty, set the new rdt3_sender_ctx as the head
    if (*head == NULL) {
        *head = newrdt3_sender_ctx;
        return;
    }

    // Traverse the list to find the last rdt3_sender_ctx
    struct rdt3_sender_ctx* current = *head;
    while (current->next != NULL) {
        current = current->next;
    }

    // Insert the new rdt3_sender_ctx at the end
    current->next = newrdt3_sender_ctx;
}


// Query a rdt3_sender_ctx in the linked list by index
struct rdt3_sender_ctx* query_ctx(struct rdt3_sender_ctx *head, unsigned ip, unsigned short port) {
    // Check if the list is empty
    if (head == NULL) {
        return NULL;
    }

    struct rdt3_sender_ctx* current = head;

    while (current != NULL && (current->ip != ip || current->port != port) ) {
        current = current->next;
    }

    // If the index is out of bounds, return NULL
    if (current == NULL) {
        return NULL;
    }

    return current;
}

// Remove a context by index
void remove_ctx(struct rdt3_sender_ctx **head, int index) {
    // Check if the list is empty
    if (*head == NULL) {
        return;
    }

	if (index == 0) {
		struct rdt3_sender_ctx* next = (*head)->next;
		free(*head);
		*head = next;
		return;
	}

    struct rdt3_sender_ctx* prev = *head;
	struct rdt3_sender_ctx* curr = (*head)->next;
	int idx = 1;
    while (curr != NULL) {
		if (idx == index) {
			struct rdt3_sender_ctx* next = curr->next;
			free(curr);
			prev->next = next;
			return;
		}
		prev = curr;
        curr = curr->next;
		idx += 1;
    }
}

/* Query the ip and port list */
struct node* send_return(int sockfd, struct sockaddr_in cltaddr, char file_idx, struct node* current, char seq) {
	unsigned int ip;
	unsigned short port;

    /***********************************************
	 * Start from current node to search for the linked
	 * node list. Until you find a node that has the
	 * file and enables file sharing.
	 *
	 * START YOUR CODE HERE
	 **********************************************/

	 // 从当前节点开始搜索
	while (current != NULL) {
		// 检查节点是否启用文件共享且拥有请求的文件
		if ((current->register_flag & FILE_FLAG) && 
			(current->file_map & (1U << (31 - file_idx)))) {
			// 找到匹配的节点，准备发送响应
			break;
		}
		current = current->next;
    }

	if (current == NULL) {
        return NULL; // 没有节点有这个文件
    }

	/***********************************************
	 * END OF YOUR CODE
	 **********************************************/

	char buffer[MAXMSG];
	
	bzero(buffer, MAXMSG);

	/* Compose send buffer: REGISTER IP Port */
	int total_len = 0;

	memcpy(buffer, &seq, sizeof(seq));
	total_len ++; /* add a seq */

	buffer[total_len] = ' ';
	total_len ++; /* add a blank */

	memcpy(buffer + total_len, RESPONSE, strlen(RESPONSE));
	total_len += strlen(RESPONSE);

	buffer[total_len] = ' ';
	total_len ++; /* add a blank */

	memcpy(buffer + total_len, &current->ip, sizeof(current->ip));
	total_len += sizeof(current->ip);

	memcpy(buffer + total_len, &current->port, sizeof(current->port));
	total_len += sizeof(current->port);

	memcpy(buffer + total_len, current->name, sizeof(current->name));
	total_len += sizeof(current->name);

	buffer[total_len] = '\0';

	sendto(sockfd, (const char *)buffer, total_len,
		0, (const struct sockaddr *) &cltaddr, sizeof(cltaddr));

	return current;
}


/* Send finish to the client */
int send_finish(int sockfd, struct sockaddr_in servaddr, char seq) {
	char buffer[MAXMSG];
	
	bzero(buffer, MAXMSG);

	/* Compose send buffer: REGISTER IP Port */
	int total_len = 0;

	memcpy(buffer, &seq, sizeof(seq));
	total_len ++; /* add a seq */

	buffer[total_len] = ' ';
	total_len ++; /* add a blank */

	memcpy(buffer + total_len, FINISH, strlen(FINISH));
	total_len += strlen(FINISH);

	buffer[total_len] = ' ';
	total_len ++; /* add a blank */

	buffer[total_len] = '\0';

	sendto(sockfd, (const char *)buffer, total_len,
		0, (const struct sockaddr *) &servaddr, sizeof(servaddr));
	
	return 0;
}


int check_timeout(long long now, struct rdt3_sender_ctx *ctx_head, int sockfd) {
	struct rdt3_sender_ctx *current = ctx_head;

    if (current == NULL) {
        return 0;
    }

    while (current != NULL) {
		/***********************************************
		 * Traverse the context list and find all the
		 * waiting for ACK and timeout packets. Resend
		 * them.
		 * Hints: the context has the packet sent time,
		 * waiting status. You also have the current time
		 * and TIMEOUT value.
		 *
		 * START YOUR CODE HERE
		 **********************************************/

		// In the check_timeout function, modify it to be more aggressive with retransmissions
		if (current->waiting_ack) {
			// Reduce the timeout or make the condition more lenient
			if (now - current->clock > TIMEOUT/2) {  // Make timeout more aggressive
				printf("Timeout detected for node with ip=%u, port=%hu\n", 
					current->ip, current->port);
				
				struct sockaddr_in cltaddr;
				memset(&cltaddr, 0, sizeof(cltaddr));
				cltaddr.sin_family = AF_INET;
				cltaddr.sin_addr.s_addr = current->ip;
				cltaddr.sin_port = htons(current->port);
				
				if (current->noack_node != NULL) {
					// Consider adding a maximum retry count to avoid infinite retransmissions
					send_return(sockfd, cltaddr, current->file_idx, 
							current->noack_node, current->noack_num);
					
					// Update the timestamp to restart the timeout timer
					current->clock = now;
				}
			}
		}

		/***********************************************
		 * END OF YOUR CODE
		 **********************************************/
        current = current->next;
    }

	return 0;
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
	for (int j = i; j < *fd_count - 1; j++)
		pfds[j] = pfds[j + 1];
    (*fd_count)--;
}

// Routine for chatting
void* chat_server(void* arguments) {
	struct args* a = (struct args*)arguments;
	struct node** node_head = a->node_head;
	struct rdt3_sender_ctx** ctx_head = a->ctx_head;
	int server_fd, client_fd;
	struct sockaddr_in address;
	int opt = 1;
	int addrlen = sizeof(address);
	char buffer[MAXMSG];
	bzero(buffer, sizeof(buffer));
	int recv_nbytes;

	int fd_count = 0;
	int fd_size = 100;
	struct pollfd* pfds = malloc(sizeof(struct pollfd) * fd_size);

	if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
		perror("socket failed");
		exit(EXIT_FAILURE);
	}

	// if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
		perror("setsockopt error");
		exit(EXIT_FAILURE);
	}

	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(SERVER_PORT);

	if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
		perror("bind failed");
		exit(EXIT_FAILURE);
	}

	if (listen(server_fd, 10) < 0) {
        perror("listen fails to start");
        exit(EXIT_FAILURE);
	}

	printf("Chat server is listening ... \n");

	/***********************************************
	 * Initialize the pfds here.
	 *
	 * START YOUR CODE HERE
	 **********************************************/
	pfds[0].fd = server_fd;
	pfds[0].events = POLLIN;
	fd_count = 1;

	/***********************************************
	 * END OF YOUR CODE
	 **********************************************/

	while (1) {
		int poll_count = poll(pfds, fd_count, -1);

		if (poll_count == -1) {
			perror("poll error");
			exit(1);
		}

		for (int i = 0; i < fd_count; i++) {
			if (pfds[i].revents & POLLIN) {
				if (pfds[i].fd == server_fd) {
					/***********************************************
					 * Add your code here to receive a new connection.
					 *
					 * START YOUR CODE HERE
					 **********************************************/
					if ((client_fd = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen))<0)
					{
						perror("accept");
    					continue;
					}
					printf("New connection accepted\n");
					add_to_pfds(&pfds, client_fd, &fd_count, &fd_size);


					/***********************************************
					 * END OF YOUR CODE
					 **********************************************/
				}
				else {
					client_fd = pfds[i].fd;
					// EXIT
					if ((recv_nbytes = recv(client_fd, buffer, MAXMSG, 0)) <= 0) {
						if (recv_nbytes == 0) {
							printf("socket %d hung up\n", client_fd);
						}
						else {
							perror("recv");
						}

						char name[MAXNAME];
						bzero(name, sizeof(name));
						char flag;
						int r = query_name_flag_by_idx(*node_head, i - 1, name, &flag);
						if (r == -1) {
							printf("Can not find %d-th name!\n", i - 1);
						}
						else if (!(flag & CHAT_FLAG)) {
							printf("This client has not register for chatting!\n");
						}
						else {
							char msg[MAXMSG];
							bzero(msg, sizeof(msg));
							strcat(msg, "'");
							strcat(msg, name);
							strcat(msg, "' has left!");
							/***********************************************
							 * Send this message that someone has left from
							 * the chatroom to other users in chat.
							 *
							 * START YOUR CODE HERE
							 **********************************************/
							for (int j = 1; j < fd_count; j++) {
								if (j != i) {
									if (send(pfds[j].fd, msg, MAXMSG, 0) < 0) {
										perror("send");
									}
								}
							}
							/***********************************************
							 * END OF YOUR CODE
							 **********************************************/
						}
						close(client_fd);
						del_from_pfds(pfds, i, &fd_count);
						remove_node(node_head, i - 1); // skip server socket
						remove_ctx(ctx_head, i - 1); // skip server socket
					}
					else {
						// @ALL: message
						if (strncmp(buffer, BROADCAST, strlen(BROADCAST)) == 0) {
							char msg[MAXMSG];
							bzero(msg, sizeof(msg));
							char name[MAXNAME];
							bzero(name, sizeof(name));
							char flag;
							if (query_name_flag_by_idx(*node_head, i - 1, name, &flag) == -1) {
								printf("Can not find %d-th name!\n", i - 1);
								continue;
							}
							if (!(flag & CHAT_FLAG)) {
								printf("This client has not register for chatting!\n");
								continue;
							}
							strcat(msg, "'");
							strcat(msg, name);
							strcat(msg, "' @ all: ");
							strcat(msg, buffer + strlen(BROADCAST));
							/***********************************************
							 * Send this message to other users in chat,
							 * just like what you do when dealing with exit.
							 *
							 * START YOUR CODE HERE
							 **********************************************/

							// 处理@ALL:消息
							for (int j = 1; j < fd_count; j++) { // 从1开始跳过服务器socket
								if (j != i) { // 不发给发送者自己
									if (send(pfds[j].fd, msg, MAXMSG, 0) < 0) {
										perror("send");
									}
								}
							}

							/***********************************************
							 * END OF YOUR CODE
							 **********************************************/
							continue;
						}
						// WHO
						if (strncmp(buffer, WHO, strlen(WHO)) == 0) {
							char names[MAXMSG];
							bzero(names, sizeof(names));
							strcat(names, "Users in chatting: ");
							collect_names_in_chatting(*node_head, i - 1, names); // skip server socket
							if (send(client_fd, names, MAXMSG, 0) < 0) {
								perror("send");
							}
							continue;
						}
						// @ name message
						if (strncmp(buffer, AT, strlen(AT)) == 0) {
							char src_name[MAXNAME];
							bzero(src_name, sizeof(src_name));
							char flag;
							if (query_name_flag_by_idx(*node_head, i - 1, src_name, &flag) == -1) {
								printf("Can not find %d-th name!\n", i - 1);
								continue;
							}
							if (!(flag & CHAT_FLAG)) {
								printf("This client has not register for chatting!\n");
								continue;
							}
							char dst_name[MAXNAME];
							bzero(dst_name, sizeof(dst_name));
							char msg[MAXMSG];
							bzero(msg, sizeof(msg));
							strcat(msg, "'");
							strcat(msg, src_name);
							strcat(msg, "' @ you: ");
							int parse_idx = sizeof(AT);
							memcpy(dst_name, buffer + parse_idx, sizeof(dst_name));
							parse_idx += sizeof(dst_name);
							strcat(msg, buffer + parse_idx);
							int j;
							if (query_idx_flag_by_name(*node_head, dst_name, &j, &flag) == -1) {
								printf("Non-existent name!\n");
								continue;
							}
							if (!(flag & CHAT_FLAG)) {
								printf("This @ client has not register for chatting!\n");
								continue;
							}
							j++; // skip the server socket
							if (j < 1 || j >= fd_count) {
								char err[] = "At failed: name does not exist!";
								printf("%s\n", err);
								if (send(pfds[i].fd, err, sizeof(err), 0) < 0) {
									perror("send");
								}
							}
							else if (j == i) {
								char err[] = "At failed: you are telling yourself!";
								printf("%s\n", err);
								if (send(pfds[i].fd, err, sizeof(err), 0) < 0) {
									perror("send");
								}
							}
							else {
								if (send(pfds[j].fd, msg, MAXMSG, 0) < 0) {
									perror("send");
								}
							} 
							continue;
						}
						// SYS@ALL:WELCOME/GOODBYE
						if (strncmp(buffer, SYSINFO, strlen(SYSINFO)) == 0) {
							char info[MAXMSG];
							bzero(info, sizeof(info));
							strcpy(info, buffer + strlen(SYSINFO));
							char msg[MAXMSG];
							bzero(msg, sizeof(msg));
							char name[MAXNAME];
							bzero(name, sizeof(name));
							char flag;
							if (query_name_flag_by_idx(*node_head, i - 1, name, &flag) == -1) {
								printf("Can not find %d-th name!\n", i - 1);
								continue;
							}
							if ((strncmp(info, WELCOME, strlen(WELCOME)) == 0 && !(flag & CHAT_FLAG))
								|| (strncmp(info, GOODBYE, strlen(GOODBYE)) == 0 && (flag & CHAT_FLAG))) {
								printf("info %s and flag %d are contradictory!\n", info, flag);
								continue;
							}
							strcat(msg, "'");
							strcat(msg, name);
							if (strncmp(buffer + strlen(SYSINFO), WELCOME, strlen(WELCOME)) == 0)
								strcat(msg, "' has joined!");
							else
								strcat(msg, "' has left!");
							/***********************************************
							 * Send this message that someone has left from
							 * or joined the chatroom to other users in chat,
							 * just like what you do when dealing with exit.
							 *
							 * START YOUR CODE HERE
							 **********************************************/

							// 处理离开和加入消息
							for (int j = 1; j < fd_count; j++) { // 从1开始跳过服务器socket
								if (j != i) { // 不发给发送者自己
									if (send(pfds[j].fd, msg, MAXMSG, 0) < 0) {
										perror("send");
									}
								}
							}

							/***********************************************
							 * END OF YOUR CODE
							 **********************************************/
							continue;
						}
					}
				}
			}
		}
	}
}

int main() {
	int sockfd;
	char buffer[MAXMSG];

	struct sockaddr_in servaddr, clientaddr;
	
	// Creating socket file descriptor
	if ( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
		perror("Socket creation failed");
		exit(EXIT_FAILURE);
	}
	
	memset(&servaddr, 0, sizeof(servaddr));
	memset(&clientaddr, 0, sizeof(clientaddr));
	
	// Filling server information
	servaddr.sin_family = AF_INET; // IPv4
	servaddr.sin_addr.s_addr = INADDR_ANY;
	servaddr.sin_port = htons(SERVER_PORT);
	
	// Bind the socket with the server address
	if ( bind(sockfd, (const struct sockaddr *)&servaddr,
				sizeof(servaddr)) < 0 )
	{
		perror("Bind failed");
		exit(EXIT_FAILURE);
	}
	
	unsigned int len;
	int n;

	struct node * node_head = init_node_list();
	struct rdt3_sender_ctx *ctx_head = init_rdt3_sender_ctx_list();

	struct rdt3_sender_ctx *current_ctx;

	unsigned parse_idx = 0;
	char seq;

	char send_buf[MAXMSG];
	unsigned send_idx = 0;

	set_timeout(sockfd, TIMEOUT);

	printf("The server is listening ...\n");
	// start chat server
	pthread_t chat_thread;
	struct args a;
	a.node_head = &node_head;
	a.ctx_head = &ctx_head;
	pthread_create(&chat_thread, NULL, chat_server, &a);

	while (1) {
		len = sizeof(clientaddr); //len is value/resuslt

		n = recvfrom(sockfd, (char *)buffer, MAXMSG,
					0, ( struct sockaddr *) &clientaddr, &len);
		if (n < 0) {
			// Checking timeout
			long long now = get_current_time();
			check_timeout(now, ctx_head, sockfd);

			continue;
		}

		buffer[n] = '\0';

		// First, try to recover context based on ip port
		unsigned ip = clientaddr.sin_addr.s_addr;
		unsigned short port = ntohs(clientaddr.sin_port);

		current_ctx = query_ctx(ctx_head, ip, port);

		if (current_ctx == NULL) {
			insert_ctx(&ctx_head, ip, port);
		}

		seq = buffer[0];
		parse_idx += 2; /* skip seq and blank */

		// Second, parse incoming packets
		// REGISTER ip port name register_flag
		if (strncmp(buffer + parse_idx, REGISTER, strlen(REGISTER)) == 0) {
			unsigned dst_ip;
			unsigned short dst_port;
			char name[MAXNAME];
			bzero(name, sizeof(name));
			char register_flag;

			parse_idx += strlen(REGISTER);
			parse_idx ++; /*skip blank */

			memcpy(&dst_ip, buffer + parse_idx, sizeof(dst_ip));
			parse_idx += sizeof(dst_ip);

			memcpy(&dst_port, buffer + parse_idx, sizeof(dst_port));
			parse_idx += sizeof(dst_port);

			memcpy(name, buffer + parse_idx, sizeof(name));
			parse_idx += sizeof(name);

			memcpy(&register_flag, buffer + parse_idx, sizeof(register_flag));
			parse_idx += sizeof(register_flag);

			printf("Register '%s' @ (%d %hd) with flag %d\n", name, dst_ip, dst_port, register_flag);

			struct node* n;
			char register_return_code = 0;
			if ((n = query_node(node_head, dst_ip, dst_port, name)) == NULL) {
				insert_node(&node_head, dst_ip, dst_port, 0, name, register_flag);
				register_return_code = 0;
			} else {
				if (n->ip == dst_ip && n->port == dst_port) {
					register_return_code = 1;
				}
				else {
					register_return_code = 2;
				}
			}

			/* send ACK packet */
			memcpy(send_buf, &seq, sizeof(seq));
			// use blank to store return code of register
			memcpy(send_buf + 1, &register_return_code, sizeof(register_return_code));
			send_idx += 2; /* seq and blank */

			memcpy(send_buf + send_idx, ACK, strlen(ACK));
			send_idx += strlen(ACK);

			sendto(sockfd, (const char *)send_buf, send_idx,
				0, (const struct sockaddr *) &clientaddr, sizeof(clientaddr));
		}


		// TODO: UPDATE ip port file_map updated_flag
		if (strncmp(buffer + parse_idx, UPDATE, strlen(UPDATE)) == 0) {
			unsigned dst_ip;
			unsigned short dst_port;
			unsigned new_map;
			char updated_flag;

			/***********************************************
			 * Refer to REGISTER implementation above and
			 * the UPDATE protocol description. Dealing with
			 * UPDATE packet.
			 *
			 * START YOUR CODE HERE
			 **********************************************/
			parse_idx += strlen(UPDATE);
			parse_idx++; /*skip blank */

			memcpy(&dst_ip, buffer + parse_idx, sizeof(dst_ip));
			parse_idx += sizeof(dst_ip);

			memcpy(&dst_port, buffer + parse_idx, sizeof(dst_port));
			parse_idx += sizeof(dst_port);

			memcpy(&new_map, buffer + parse_idx, sizeof(new_map));
			parse_idx += sizeof(new_map);

			memcpy(&updated_flag, buffer + parse_idx, sizeof(updated_flag));
			parse_idx += sizeof(updated_flag);

			printf("Update '%s' @ (%d %hd) with flag %d\n", "unknown", dst_ip, dst_port, updated_flag);

			/***********************************************
			 * END OF YOUR CODE
			 **********************************************/

			struct node *update_node = query_node(node_head, dst_ip, dst_port, NULL);

			if (update_node == NULL) {
				printf("UPDATE Failed: node does not exists!");
			} else {
				/***********************************************
				 * Update the file_map of the corresponding node
				 * and send an ACK message back.
				 *
				 * START YOUR CODE HERE
				 **********************************************/

				// 更新节点信息
				update_node->file_map = new_map;
				update_node->register_flag = updated_flag;
				printf("Updated node: '%s' with file_map 0x%x and flag %d\n", 
					update_node->name, update_node->file_map, update_node->register_flag);
		
				// 发送ACK包
				memcpy(send_buf, &seq, sizeof(seq));
				send_idx += 2; /* seq and blank */
		
				memcpy(send_buf + send_idx, ACK, strlen(ACK));
				send_idx += strlen(ACK);
		
				sendto(sockfd, (const char *)send_buf, send_idx,
					0, (const struct sockaddr *) &clientaddr, sizeof(clientaddr));

				/***********************************************
				 * END OF YOUR CODE
				 **********************************************/
			}
		}


		// QUERY xx.txt
		if (strncmp(buffer + parse_idx, QUERY, strlen(QUERY)) == 0) {
			char file_idx;
			char file_name[20];

			parse_idx += strlen(QUERY);
			parse_idx ++; /*skip blank */

			memcpy(&file_name, buffer + parse_idx, 10);
			file_idx = (file_name[0] - '0') * 10 + (file_name[1] - '0');
			if (file_idx < 0 || file_idx > 31) {
				printf("Invalid file name: %s", file_name);
				continue;
			}

			/* send ACK packet */
			memcpy(send_buf, &seq, sizeof(seq));
			send_idx += 2; /* seq and blank */

			memcpy(send_buf + send_idx, ACK, strlen(ACK));
			send_idx += strlen(ACK);

			sendto(sockfd, (const char *)send_buf, send_idx,
					0, (const struct sockaddr *) &clientaddr, sizeof(clientaddr));

			// Send the first RESPONSE message
			// Whenever receive QUERY, initiate the context
			struct node *current_node = send_return(sockfd, clientaddr, file_idx, node_head, SEQ0);

			if (current_node == NULL) {
				char new_seq = 1 - seq; /* 1 -> 0, 0 -> 1 */
				/* send FINISH packet */
				send_finish(sockfd, clientaddr, new_seq);
				current_ctx->waiting_ack = 0;
			} else {
				// Update context
				current_ctx->waiting_ack = 1;
				current_ctx->clock = get_current_time();
				current_ctx->noack_num = SEQ0;
				current_ctx->noack_node = current_node;
				current_ctx->file_idx = file_idx;
			}
		}


		// ACK
		if (strncmp(buffer + 2, ACK, strlen(ACK)) == 0) {

			char new_seq = 1 - seq; /* 1 -> 0, 0 -> 1 */

			if (current_ctx->waiting_ack && current_ctx->noack_num == seq) {
				current_ctx->clock = get_current_time();
				current_ctx->noack_num = new_seq;
				current_ctx->noack_node = send_return(sockfd, clientaddr,
						current_ctx->file_idx, current_ctx->noack_node->next, new_seq);

				if (current_ctx->noack_node == NULL) {
					/* send FINISH packet */
					send_finish(sockfd, clientaddr, new_seq);
					current_ctx->waiting_ack = 0;
				}

			}
		}

		bzero(buffer, MAXMSG);
		bzero(send_buf, MAXMSG);
		parse_idx = 0;
		send_idx = 0;
	}
	return 0;
}
