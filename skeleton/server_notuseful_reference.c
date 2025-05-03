#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <poll.h>
#include <errno.h>
#include <time.h>
#include <signal.h>

#define SERVER "127.0.0.1"
#define SERVER_PORT 5000

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

// Ultra aggressive settings for extreme packet loss
#define TIMEOUT 100000       // 150 ms - even shorter timeout
#define MAX_RETRIES 100       // Even more aggressive retries
#define ACK_SEND_COUNT 20    // Send many copies of ACKs
#define RESP_SEND_COUNT 50   // Send many copies of RESPONSE messages
#define FINISH_SEND_COUNT 60 // Send many copies of FINISH messages
#define RETRY_INTERVAL 3000  // 5 ms between retransmissions

// Special debug flag
#define DEBUG_MODE 1

// Special debugging macro
#define DEBUG_PRINT(fmt, ...) \
    if (DEBUG_MODE) { \
        printf("[%s] [%s:%d] " fmt "\n", \
               get_time_string(), __func__, __LINE__, ##__VA_ARGS__); \
        fflush(stdout); \
    }

// Global variable to track if server is running
volatile sig_atomic_t server_running = 1;

// Customized signal handler for clean shutdown
void signal_handler(int sig) {
    server_running = 0;
}

// Get a time string for debugging
char* get_time_string() {
    static char time_str[64];
    struct timeval tv;
    struct tm* tm_info;
    
    gettimeofday(&tv, NULL);
    tm_info = localtime(&tv.tv_sec);
    
    sprintf(time_str, "%02d:%02d:%02d.%03ld", 
            tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec, 
            tv.tv_usec / 1000);
    
    return time_str;
}

/* This structure can be used to parse packet */
struct ip_port {
    unsigned int ip;
    unsigned short port;
};

struct node {
    unsigned int ip;        /* ip and port are used as index */
    unsigned short port;    /* the port number of TCP listening */
    unsigned int file_map;  /* a bitmap for files */
    char name[MAXNAME];     /* a unique username */
    char register_flag;     /* 1: file sharing, 2: chatting, 3: both */
    struct node *next;
};

struct rdt3_sender_ctx {
    unsigned int ip;            /* ip and port are used as index */
    unsigned short port;        /* the port number of current socket */
    char waiting_ack;           /* if waiting ack */
    char noack_num;             /* waiting ack num */
    char file_idx;              /* file index for QUERY & RESPONSE */
    long long clock;            /* the clock time of last packet sending, for timeout */
    int retry_count;            /* count of retries for current transmission */
    int success_count;          /* count of successful transmissions */
    struct node *noack_node;    /* waiting ack node, maybe retransmitted */
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
        DEBUG_PRINT("Memory allocation failed for node");
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
    DEBUG_PRINT("Added new node: IP=%u, port=%hu, name=%s, flag=%d", 
               ip, port, name, register_flag);
}

// Query a node in the linked list
struct node* query_node(struct node *head, unsigned ip, unsigned short port, const char* name) {
    // Check if the list is empty
    if (head == NULL) {
        return NULL;
    }

    struct node* current = head;

    while (current != NULL) {
        // If both IP and port match, or name matches (if provided)
        if ((current->ip == ip && current->port == port) || 
            (name != NULL && strcmp(current->name, name) == 0)) {
            return current;
        }
        current = current->next;
    }

    return NULL;
}

// Traverse the linked list to collect names in chatting except the node at index
void collect_names_in_chatting(struct node *head, int index, char* names) {
    struct node *current = head;
    int idx = 0;
    while (current != NULL) {
        // If the node has chat enabled and is not the current user
        if ((current->register_flag & CHAT_FLAG) && idx != index) {
            strcat(names, "'");
            strcat(names, current->name);
            strcat(names, "' ");
        }
        current = current->next;
        idx++;
    }
}

// Query index and flag by name in the linked list
int query_idx_flag_by_name(struct node *head, const char* name, int* index, char* flag) {
    struct node *current = head;
    int idx = 0;
    while (current != NULL) {
        if (strcmp(current->name, name) == 0) {
            *index = idx;
            *flag = current->register_flag;
            return 0;
        }
        current = current->next;
        idx++;
    }
    return -1;
}

// Query name and flag by index in the linked list
int query_name_flag_by_idx(struct node *head, int index, char* name, char* flag) {
    struct node *current = head;
    int idx = 0;
    while (current != NULL && idx < index) {
        current = current->next;
        idx++;
    }

    if (current == NULL) {
        return -1;
    }
    
    strcpy(name, current->name);
    *flag = current->register_flag;
    return 0;
}

// Remove a node by index
void remove_node(struct node** head, int index) {
    // Check if the list is empty
    if (*head == NULL) {
        return;
    }

    if (index == 0) { // index is head node
        struct node* next = (*head)->next;
        free(*head);
        *head = next;
        DEBUG_PRINT("Removed head node (index 0)");
        return;
    }

    struct node* prev = *head;
    struct node* curr = (*head)->next;
    int idx = 1;
    while (curr != NULL) {
        if (idx == index) {
            struct node* next = curr->next;
            free(curr);
            prev->next = next;
            DEBUG_PRINT("Removed node at index %d", index);
            return;
        }
        prev = curr;
        curr = curr->next;
        idx += 1;
    }
    
    DEBUG_PRINT("Node at index %d not found for removal", index);
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
        DEBUG_PRINT("Memory allocation failed for sender context");
        return;
    }

    newrdt3_sender_ctx->ip = ip;
    newrdt3_sender_ctx->port = port;
    newrdt3_sender_ctx->waiting_ack = 0;
    newrdt3_sender_ctx->noack_num = 0;
    newrdt3_sender_ctx->noack_node = NULL;
    newrdt3_sender_ctx->clock = 0;
    newrdt3_sender_ctx->retry_count = 0;
    newrdt3_sender_ctx->success_count = 0;
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
    DEBUG_PRINT("Added new sender context: IP=%u, port=%hu", ip, port);
}

// Query a rdt3_sender_ctx in the linked list by index
struct rdt3_sender_ctx* query_ctx(struct rdt3_sender_ctx *head, unsigned ip, unsigned short port) {
    // Check if the list is empty
    if (head == NULL) {
        return NULL;
    }

    struct rdt3_sender_ctx* current = head;

    while (current != NULL && (current->ip != ip || current->port != port)) {
        current = current->next;
    }

    // If not found, return NULL
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
        DEBUG_PRINT("Removed head sender context (index 0)");
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
            DEBUG_PRINT("Removed sender context at index %d", index);
            return;
        }
        prev = curr;
        curr = curr->next;
        idx += 1;
    }
    
    DEBUG_PRINT("Sender context at index %d not found for removal", index);
}

// Enhanced send_return function with extreme redundancy and variable patterns
struct node* send_return(int sockfd, struct sockaddr_in cltaddr, char file_idx, struct node* current, char seq) {
    // Find next node with the requested file
    while (current != NULL) {
        // Check if node has file sharing enabled and has the requested file
        if ((current->register_flag & FILE_FLAG) && 
            (current->file_map & (1U << (31 - file_idx)))) {
            // Found a matching node
            break;
        }
        current = current->next;
    }

    if (current == NULL) {
        DEBUG_PRINT("No node has file %d", file_idx);
        return NULL; // No node has this file
    }

    char buffer[MAXMSG];
    bzero(buffer, MAXMSG);

    /* Construct sending buffer: RESPONSE IP Port Name */
    int total_len = 0;

    memcpy(buffer, &seq, sizeof(seq));
    total_len ++; /* Add sequence number */

    buffer[total_len] = ' ';
    total_len ++; /* Add space */

    memcpy(buffer + total_len, RESPONSE, strlen(RESPONSE));
    total_len += strlen(RESPONSE);

    buffer[total_len] = ' ';
    total_len ++; /* Add space */

    memcpy(buffer + total_len, &current->ip, sizeof(current->ip));
    total_len += sizeof(current->ip);

    memcpy(buffer + total_len, &current->port, sizeof(current->port));
    total_len += sizeof(current->port);

    memcpy(buffer + total_len, current->name, sizeof(current->name));
    total_len += sizeof(current->name);

    buffer[total_len] = '\0';

    // Ultra-aggressive approach: multiple bursts with very dynamic patterns
    int success_count = 0;
    int burst_count = 8; // More bursts than before
    int packets_per_burst = RESP_SEND_COUNT / burst_count;
    
    DEBUG_PRINT("Sending RESPONSE for file %d with seq %d in %d bursts of %d packets each",
               file_idx, seq, burst_count, packets_per_burst);
    
    // Special pattern: Send in multiple bursts with varying delays between
    for (int burst = 0; burst < burst_count; burst++) {
        // Calculate how many packets for this burst (variable pattern)
        int this_burst_packets = packets_per_burst;
        if (burst == burst_count - 1) {
            // Last burst gets any remainder
            this_burst_packets += (RESP_SEND_COUNT % burst_count);
        } else if (burst % 2 == 0) {
            // Even-numbered bursts get a bonus packet
            this_burst_packets += 1;
        }
        
        DEBUG_PRINT("RESPONSE burst %d: sending %d packets", burst, this_burst_packets);
        
        // Send this burst
        for (int i = 0; i < this_burst_packets; i++) {
            if (sendto(sockfd, (const char *)buffer, total_len,
                0, (const struct sockaddr *) &cltaddr, sizeof(cltaddr)) >= 0) {
                success_count++;
            } else {
                perror("sendto failed in send_return");
            }
            
            // Variable micro-delays within burst
            int micro_delay = 500 + (rand() % 1500); // 0.5-2ms
            usleep(micro_delay);
        }
        
        // Variable delay between bursts
        int burst_delay = 5000 + (rand() % 10000); // 5-15ms
        usleep(burst_delay);
    }

    DEBUG_PRINT("RESPONSE for file %d sent with %d/%d successful attempts", 
           file_idx, success_count, RESP_SEND_COUNT);
    DEBUG_PRINT("[RDT3.0] DATA: sent RESPONSE seq=%d for file %d to %s:%d, success=%d/%d", 
    seq, file_idx, inet_ntoa(cltaddr.sin_addr), ntohs(cltaddr.sin_port),
    success_count, RESP_SEND_COUNT);
    return current;
}

// Enhanced send_finish function with extreme redundancy
int send_finish(int sockfd, struct sockaddr_in servaddr, char seq) {
    char buffer[MAXMSG];
    
    bzero(buffer, MAXMSG);

    /* Construct sending buffer: FINISH */
    int total_len = 0;

    memcpy(buffer, &seq, sizeof(seq));
    total_len ++; /* Add sequence number */

    buffer[total_len] = ' ';
    total_len ++; /* Add space */

    memcpy(buffer + total_len, FINISH, strlen(FINISH));
    total_len += strlen(FINISH);

    buffer[total_len] = ' ';
    total_len ++; /* Add space */

    buffer[total_len] = '\0';

    // Ultra-aggressive approach: multiple bursts with very dynamic patterns
    int success_count = 0;
    int burst_count = 8; // Even more bursts for FINISH
    int packets_per_burst = FINISH_SEND_COUNT / burst_count;
    
    DEBUG_PRINT("Sending FINISH with seq %d in %d bursts of %d packets each",
               seq, burst_count, packets_per_burst);
    
    // Special pattern: Send in multiple bursts with varying delays between
    for (int burst = 0; burst < burst_count; burst++) {
        // Calculate how many packets for this burst (variable pattern)
        int this_burst_packets = packets_per_burst;
        if (burst == burst_count - 1) {
            // Last burst gets any remainder
            this_burst_packets += (FINISH_SEND_COUNT % burst_count);
        } else if (burst % 2 == 0) {
            // Even-numbered bursts get a bonus packet
            this_burst_packets += 1;
        }
        
        DEBUG_PRINT("FINISH burst %d: sending %d packets", burst, this_burst_packets);
        
        // Send this burst
        for (int i = 0; i < this_burst_packets; i++) {
            if (sendto(sockfd, (const char *)buffer, total_len,
                    0, (const struct sockaddr *) &servaddr, sizeof(servaddr)) >= 0) {
                success_count++;
            } else {
                perror("sendto failed in send_finish");
            }
            
            // Variable micro-delays within burst
            int micro_delay = 500 + (rand() % 1500); // 0.5-2ms
            usleep(micro_delay);
        }
        
        // Variable delay between bursts
        int burst_delay = 5000 + (rand() % 10000); // 5-15ms
        usleep(burst_delay);
    }
    
    DEBUG_PRINT("FINISH sent with %d/%d successful attempts", success_count, FINISH_SEND_COUNT);
    return 0;
}

// Enhanced send_ack function with extreme redundancy
int send_ack(int sockfd, struct sockaddr_in cltaddr, char seq, char return_code) {
    char buffer[MAXMSG];
    bzero(buffer, MAXMSG);

    // Construct ACK message
    int send_idx = 0;
    
    memcpy(buffer, &seq, sizeof(seq));
    // Use blank to store return code
    memcpy(buffer + 1, &return_code, sizeof(return_code));
    send_idx += 2; /* seq and blank */

    memcpy(buffer + send_idx, ACK, strlen(ACK));
    send_idx += strlen(ACK);

    // Ultra-aggressive approach: multiple bursts with very dynamic patterns
    int success_count = 0;
    int burst_count = 4; // Multiple bursts for ACK
    int packets_per_burst = ACK_SEND_COUNT / burst_count;
    
    DEBUG_PRINT("Sending ACK with seq %d in %d bursts of %d packets each",
               seq, burst_count, packets_per_burst);
    
    // Special pattern: Send in multiple bursts with varying delays between
    for (int burst = 0; burst < burst_count; burst++) {
        // Calculate how many packets for this burst (variable pattern)
        int this_burst_packets = packets_per_burst;
        if (burst == burst_count - 1) {
            // Last burst gets any remainder
            this_burst_packets += (ACK_SEND_COUNT % burst_count);
        } else if (burst % 2 == 0) {
            // Even-numbered bursts get a bonus packet
            this_burst_packets += 1;
        }
        
        DEBUG_PRINT("ACK burst %d: sending %d packets", burst, this_burst_packets);
        
        // Send this burst
        for (int i = 0; i < this_burst_packets; i++) {
            if (sendto(sockfd, (const char *)buffer, send_idx,
                    0, (const struct sockaddr *) &cltaddr, sizeof(cltaddr)) >= 0) {
                success_count++;
            } else {
                perror("sendto failed in send_ack");
            }
            
            // Variable micro-delays within burst
            int micro_delay = 500 + (rand() % 1500); // 0.5-2ms
            usleep(micro_delay);
        }
        
        // Variable delay between bursts
        int burst_delay = 3000 + (rand() % 5000); // 3-8ms
        usleep(burst_delay);
    }
    
    DEBUG_PRINT("ACK sent with %d/%d successful attempts", success_count, ACK_SEND_COUNT);
    DEBUG_PRINT("[RDT3.0] ACK: sent seq=%d to %s:%d, success=%d/%d", 
        seq, inet_ntoa(cltaddr.sin_addr), ntohs(cltaddr.sin_port), 
        success_count, ACK_SEND_COUNT);

    return success_count;
}

// Enhanced timeout check with adaptive timeouts and better retransmission
int check_timeout(long long now, struct rdt3_sender_ctx *ctx_head, int sockfd) {
    struct rdt3_sender_ctx *current = ctx_head;
    int retrans_count = 0;

    if (current == NULL) {
        return 0;
    }

    while (current != NULL) {
        if (current->waiting_ack) {
            // Check if timed out - adaptive timeout based on retry count
            long long elapsed = now - current->clock;
            
            // Adaptive timeout - gets more aggressive with more retries
            long long timeout = TIMEOUT;
            if (current->retry_count > 0) {
                // Gradually reduce timeout for more aggressive retries
                timeout = TIMEOUT / (1 + (current->retry_count / 5));
                
                // Minimum timeout floor
                if (timeout < 20000) { // 20ms最小值 (从30ms)
                    timeout = 20000;
                }
            }
            
            if (elapsed > timeout) {
                DEBUG_PRINT("Timeout detected for node with ip=%u, port=%hu (retry %d, elapsed %lld ms)", 
                           current->ip, current->port, current->retry_count, elapsed/1000);

                DEBUG_PRINT("[RDT3.0] TIMEOUT: seq=%d, node=%s, retry=%d/%d, elapsed=%lld ms", 
                    current->noack_num, 
                    current->noack_node ? current->noack_node->name : "FINISH", 
                    current->retry_count, MAX_RETRIES, 
                    elapsed/1000);
                
                // Create client address structure
                struct sockaddr_in cltaddr;
                memset(&cltaddr, 0, sizeof(cltaddr));
                cltaddr.sin_family = AF_INET;
                cltaddr.sin_addr.s_addr = current->ip;
                cltaddr.sin_port = htons(current->port);
                
                // Increment retry counter
                current->retry_count++;
                
                if (current->retry_count >= MAX_RETRIES) {
                    DEBUG_PRINT("Max retries reached for client %u:%hu, giving up", 
                               current->ip, current->port);
                    DEBUG_PRINT("[RDT3.0] MAX RETRIES: giving up on seq=%d after %d attempts, file=%d", 
                    current->noack_num, current->retry_count, current->file_idx);
                    current->waiting_ack = 0; // Give up after too many retries
                } else {
                    if (current->noack_node != NULL) {
                        // Retransmit RESPONSE message
                        DEBUG_PRINT("Retransmitting RESPONSE for file %d (retry %d)", 
                                   current->file_idx, current->retry_count);
                        DEBUG_PRINT("[RDT3.0] RETRANS DATA: seq=%d, file=%d, retry=%d/%d, elapsed=%lld ms", 
                        current->noack_num, current->file_idx, 
                        current->retry_count, MAX_RETRIES, elapsed/1000);
                        send_return(sockfd, cltaddr, current->file_idx, 
                                  current->noack_node, current->noack_num);
                    } else {
                        // If no more nodes, send FINISH message
                        DEBUG_PRINT("Retransmitting FINISH (retry %d)", current->retry_count);
                        send_finish(sockfd, cltaddr, current->noack_num);
                    }
                    
                    // Update timestamp
                    current->clock = now;
                    retrans_count++;
                }
            }
        }
        current = current->next;
    }

    return retrans_count;
}

// Add a new file descriptor to the set
void add_to_pfds(struct pollfd *pfds[], int newfd, int *fd_count, int *fd_size) {
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
void del_from_pfds(struct pollfd pfds[], int i, int *fd_count) {
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

    DEBUG_PRINT("Chat server is listening on port %d", SERVER_PORT);

    pfds[0].fd = server_fd;
    pfds[0].events = POLLIN;
    fd_count = 1;

    while (server_running) {
        int poll_count = poll(pfds, fd_count, 1000); // 1 second timeout to check server_running flag

        if (poll_count == -1) {
            if (errno == EINTR) {
                // Interrupted by signal, check if we should exit
                continue;
            }
            perror("poll error");
            break;
        }
        
        if (poll_count == 0) {
            // Timeout, just continue to check server_running flag
            continue;
        }

        for (int i = 0; i < fd_count; i++) {
            if (pfds[i].revents & POLLIN) {
                if (pfds[i].fd == server_fd) {
                    // Accept new connection
                    if ((client_fd = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen)) < 0) {
                        perror("accept");
                        continue;
                    }
                    DEBUG_PRINT("New connection accepted from %s:%d", 
                               inet_ntoa(address.sin_addr), ntohs(address.sin_port));
                    add_to_pfds(&pfds, client_fd, &fd_count, &fd_size);
                }
                else {
                    client_fd = pfds[i].fd;
                    // Handle EXIT
                    if ((recv_nbytes = recv(client_fd, buffer, MAXMSG, 0)) <= 0) {
                        if (recv_nbytes == 0) {
                            DEBUG_PRINT("Socket %d hung up", client_fd);
                        }
                        else {
                            perror("recv");
                        }

                        char name[MAXNAME];
                        bzero(name, sizeof(name));
                        char flag;
                        int r = query_name_flag_by_idx(*node_head, i - 1, name, &flag);
                        if (r == -1) {
                            DEBUG_PRINT("Cannot find %d-th name!", i - 1);
                        }
                        else if (!(flag & CHAT_FLAG)) {
                            DEBUG_PRINT("This client has not registered for chatting!");
                        }
                        else {
                            char msg[MAXMSG];
                            bzero(msg, sizeof(msg));
                            strcat(msg, "'");
                            strcat(msg, name);
                            strcat(msg, "' has left!");
                            
                            // Send message to other users
                            for (int j = 1; j < fd_count; j++) {
                                if (j != i) {
                                    if (send(pfds[j].fd, msg, strlen(msg), 0) < 0) {
                                        perror("send");
                                    }
                                }
                            }
                        }
                        close(client_fd);
                        del_from_pfds(pfds, i, &fd_count);
                        remove_node(node_head, i - 1); // skip server socket
                        remove_ctx(ctx_head, i - 1); // skip server socket
                    }
                    else {
                        buffer[recv_nbytes] = '\0';
                        // @ALL: message
                        if (strncmp(buffer, BROADCAST, strlen(BROADCAST)) == 0) {
                            char msg[MAXMSG];
                            bzero(msg, sizeof(msg));
                            char name[MAXNAME];
                            bzero(name, sizeof(name));
                            char flag;
                            if (query_name_flag_by_idx(*node_head, i - 1, name, &flag) == -1) {
                                DEBUG_PRINT("Cannot find %d-th name!", i - 1);
                                continue;
                            }
                            if (!(flag & CHAT_FLAG)) {
                                DEBUG_PRINT("This client has not registered for chatting!");
                                continue;
                            }
                            strcat(msg, "'");
                            strcat(msg, name);
                            strcat(msg, "' @ all: ");
                            strcat(msg, buffer + strlen(BROADCAST));
                            
                            // Broadcast to all other users
                            for (int j = 1; j < fd_count; j++) {
                                if (j != i) {
                                    if (send(pfds[j].fd, msg, MAXMSG, 0) < 0) {
                                        perror("send");
                                    }
                                }
                            }
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
                                DEBUG_PRINT("Cannot find %d-th name!", i - 1);
                                continue;
                            }
                            if (!(flag & CHAT_FLAG)) {
                                DEBUG_PRINT("This client has not registered for chatting!");
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
                                DEBUG_PRINT("Non-existent name!");
                                continue;
                            }
                            if (!(flag & CHAT_FLAG)) {
                                DEBUG_PRINT("This @ client has not registered for chatting!");
                                continue;
                            }
                            j++; // skip the server socket
                            if (j < 1 || j >= fd_count) {
                                char err[] = "At failed: name does not exist!";
                                DEBUG_PRINT("%s", err);
                                if (send(pfds[i].fd, err, sizeof(err), 0) < 0) {
                                    perror("send");
                                }
                            }
                            else if (j == i) {
                                char err[] = "At failed: you are telling yourself!";
                                DEBUG_PRINT("%s", err);
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
                                DEBUG_PRINT("Cannot find %d-th name!", i - 1);
                                continue;
                            }
                            if ((strncmp(info, WELCOME, strlen(WELCOME)) == 0 && !(flag & CHAT_FLAG))
                                || (strncmp(info, GOODBYE, strlen(GOODBYE)) == 0 && (flag & CHAT_FLAG))) {
                                DEBUG_PRINT("Info %s and flag %d are contradictory!", info, flag);
                                continue;
                            }
                            strcat(msg, "'");
                            strcat(msg, name);
                            if (strncmp(buffer + strlen(SYSINFO), WELCOME, strlen(WELCOME)) == 0)
                                strcat(msg, "' has joined!");
                            else
                                strcat(msg, "' has left!");
                            
                            // Notify other clients
                            for (int j = 1; j < fd_count; j++) {
                                if (j != i) {
                                    if (send(pfds[j].fd, msg, MAXMSG, 0) < 0) {
                                        perror("send");
                                    }
                                }
                            }
                            continue;
                        }
                    }
                }
            }
        }
    }

    // Clean up resources
    for (int i = 0; i < fd_count; i++) {
        if (pfds[i].fd >= 0) {
            close(pfds[i].fd);
        }
    }
    free(pfds);

    DEBUG_PRINT("Chat server thread exiting");
    return NULL;
}

int main() {
    int sockfd;
    char buffer[MAXMSG];

    struct sockaddr_in servaddr, clientaddr;
    
    // Set up signal handler for graceful shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Seed random for variable delays
    srand(time(NULL));
    
    // Creating socket file descriptor
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    memset(&servaddr, 0, sizeof(servaddr));
    memset(&clientaddr, 0, sizeof(clientaddr));
    
    // Set socket reuse option for quick restart
    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR failed");
    }
    
    // Filling server information
    servaddr.sin_family = AF_INET; // IPv4
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(SERVER_PORT);
    
    // Bind the socket with the server address
    if (bind(sockfd, (const struct sockaddr *)&servaddr,
                sizeof(servaddr)) < 0) {
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

    // Set initial timeout
    set_timeout(sockfd, TIMEOUT);

    DEBUG_PRINT("The server is listening on port %d...", SERVER_PORT);
    
    // Start chat server
    pthread_t chat_thread;
    struct args a;
    a.node_head = &node_head;
    a.ctx_head = &ctx_head;
    pthread_create(&chat_thread, NULL, chat_server, &a);

    int main_loop_count = 0;
    while (server_running) {
        main_loop_count++;
        len = sizeof(clientaddr);

        // Set very short timeout for non-blocking operation
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 20000; // 20ms polling interval
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

        bzero(buffer, MAXMSG);
        n = recvfrom(sockfd, (char *)buffer, MAXMSG,
                    0, (struct sockaddr *)&clientaddr, &len);
        
        // Frequently check for timeouts - multiple checks per iteration
        long long now = get_current_time();
        check_timeout(now, ctx_head, sockfd);
        
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Even more frequent timeout checks during idle periods
                now = get_current_time();
                check_timeout(now, ctx_head, sockfd);
                
                // Every 25 iterations (roughly 0.5 seconds), do an extra aggressive check 
                // and retransmit pending packets if needed
                if (main_loop_count % 10 == 0) {
                    struct rdt3_sender_ctx *current = ctx_head;
                    while (current != NULL) {
                        if (current->waiting_ack) {
                            struct sockaddr_in addr;
                            memset(&addr, 0, sizeof(addr));
                            addr.sin_family = AF_INET;
                            addr.sin_addr.s_addr = current->ip;
                            addr.sin_port = htons(current->port);
                            
                            if (current->noack_node != NULL) {
                                DEBUG_PRINT("Periodic extra retransmit RESPONSE for file %d", current->file_idx);
                                send_return(sockfd, addr, current->file_idx, 
                                          current->noack_node, current->noack_num);
                            } else {
                                DEBUG_PRINT("Periodic extra retransmit FINISH");
                                send_finish(sockfd, addr, current->noack_num);
                            }
                        }
                        current = current->next;
                    }
                }
                
                usleep(5000); // 5ms pause - shorter to check more frequently
                continue;
            } else if (errno == EINTR) {
                // Interrupted by signal, check if we should exit
                continue;
            } else {
                perror("recvfrom");
                continue;
            }
        }

        buffer[n] = '\0';
        
        // First, try to recover context based on ip port
        unsigned ip = clientaddr.sin_addr.s_addr;
        unsigned short port = ntohs(clientaddr.sin_port);

        current_ctx = query_ctx(ctx_head, ip, port);

        if (current_ctx == NULL) {
            insert_ctx(&ctx_head, ip, port);
            current_ctx = query_ctx(ctx_head, ip, port);
        }

        seq = buffer[0];
        parse_idx = 2; /* skip seq and blank */

        // Special debug for unexpected message
        if (n < parse_idx + 1) {
            DEBUG_PRINT("Warning: Received too short message (length=%d)", n);
            continue;
        }

        // REGISTER handling
        if (strncmp(buffer + parse_idx, REGISTER, strlen(REGISTER)) == 0) {
            unsigned dst_ip;
            unsigned short dst_port;
            char name[MAXNAME];
            bzero(name, sizeof(name));
            char register_flag;

            parse_idx += strlen(REGISTER);
            parse_idx++; /* skip blank */

            if (n < parse_idx + sizeof(dst_ip) + sizeof(dst_port) + sizeof(name) + sizeof(register_flag)) {
                DEBUG_PRINT("Warning: REGISTER message too short");
                continue;
            }

            memcpy(&dst_ip, buffer + parse_idx, sizeof(dst_ip));
            parse_idx += sizeof(dst_ip);

            memcpy(&dst_port, buffer + parse_idx, sizeof(dst_port));
            parse_idx += sizeof(dst_port);

            memcpy(name, buffer + parse_idx, sizeof(name));
            parse_idx += sizeof(name);

            memcpy(&register_flag, buffer + parse_idx, sizeof(register_flag));
            parse_idx += sizeof(register_flag);

            DEBUG_PRINT("Register '%s' @ (%u %hu) with flag %d", 
                       name, dst_ip, dst_port, register_flag);

            struct node* n;
            char register_return_code = 0;
            if ((n = query_node(node_head, dst_ip, dst_port, name)) == NULL) {
                insert_node(&node_head, dst_ip, dst_port, 0, name, register_flag);
                register_return_code = 0;
                DEBUG_PRINT("New node registered: %s", name);
            } else {
                if (n->ip == dst_ip && n->port == dst_port) {
                    register_return_code = 1;
                    DEBUG_PRINT("Node already registered: %s", name);
                }
                else {
                    register_return_code = 2;
                    DEBUG_PRINT("Node registration conflict: %s", name);
                }
            }

            // Send ACK using our ultra-aggressive function
            send_ack(sockfd, clientaddr, seq, register_return_code);
        }

        // UPDATE handling
        if (strncmp(buffer + parse_idx, UPDATE, strlen(UPDATE)) == 0) {
            unsigned dst_ip;
            unsigned short dst_port;
            unsigned new_map;
            char updated_flag;

            parse_idx += strlen(UPDATE);
            parse_idx++; /* skip blank */

            if (n < parse_idx + sizeof(dst_ip) + sizeof(dst_port) + sizeof(new_map) + sizeof(updated_flag)) {
                DEBUG_PRINT("Warning: UPDATE message too short");
                continue;
            }

            memcpy(&dst_ip, buffer + parse_idx, sizeof(dst_ip));
            parse_idx += sizeof(dst_ip);

            memcpy(&dst_port, buffer + parse_idx, sizeof(dst_port));
            parse_idx += sizeof(dst_port);

            memcpy(&new_map, buffer + parse_idx, sizeof(new_map));
            parse_idx += sizeof(new_map);

            memcpy(&updated_flag, buffer + parse_idx, sizeof(updated_flag));
            parse_idx += sizeof(updated_flag);

            DEBUG_PRINT("Update @ (%u %hu) with flag %d, file_map=0x%x", 
                       dst_ip, dst_port, updated_flag, new_map);

            struct node *update_node = query_node(node_head, dst_ip, dst_port, NULL);

            if (update_node == NULL) {
                DEBUG_PRINT("UPDATE Failed: node does not exist!");
            } else {
                // Update node information
                update_node->file_map = new_map;
                update_node->register_flag = updated_flag;
                DEBUG_PRINT("Updated node: '%s' with file_map 0x%x and flag %d", 
                           update_node->name, update_node->file_map, update_node->register_flag);
        
                // Send ACK using our ultra-aggressive function
                send_ack(sockfd, clientaddr, seq, 0);
            }
        }

        // QUERY handling with extra robustness
        if (strncmp(buffer + parse_idx, QUERY, strlen(QUERY)) == 0) {
            char file_idx;
            char file_name[20];

            parse_idx += strlen(QUERY);
            parse_idx++; /* skip blank */

            if (n < parse_idx + 10) { // Simple sanity check for minimum length
                DEBUG_PRINT("Warning: QUERY message too short");
                continue;
            }

            memcpy(&file_name, buffer + parse_idx, 10);
            file_idx = (file_name[0] - '0') * 10 + (file_name[1] - '0');
            if (file_idx < 0 || file_idx > 31) {
                DEBUG_PRINT("Invalid file name: %s", file_name);
                continue;
            }

            DEBUG_PRINT("Processing QUERY for file %d from %u:%hu", 
                       file_idx, clientaddr.sin_addr.s_addr, ntohs(clientaddr.sin_port));

            // Send ACK aggressively
            send_ack(sockfd, clientaddr, seq, 0);

            // Ensure context is available
            if (current_ctx == NULL) {
                insert_ctx(&ctx_head, clientaddr.sin_addr.s_addr, ntohs(clientaddr.sin_port));
                current_ctx = query_ctx(ctx_head, clientaddr.sin_addr.s_addr, ntohs(clientaddr.sin_port));
                DEBUG_PRINT("Created new context for %u:%hu", 
                           clientaddr.sin_addr.s_addr, ntohs(clientaddr.sin_port));
            }

            // Reset retry counter for new query
            current_ctx->retry_count = 0;
            current_ctx->success_count = 0;

            // First, find all nodes that have the file to build a complete list
            struct node *file_nodes[50]; // Maximum 50 nodes with the file
            int node_count = 0;
            struct node *temp_node = node_head;
            
            while (temp_node != NULL && node_count < 50) {
                if ((temp_node->register_flag & FILE_FLAG) && 
                    (temp_node->file_map & (1U << (31 - file_idx)))) {
                    file_nodes[node_count++] = temp_node;
                    DEBUG_PRINT("Found node '%s' with file %d", temp_node->name, file_idx);
                }
                temp_node = temp_node->next;
            }
            
            if (node_count == 0) {
                char new_seq = 1 - seq; /* 1 -> 0, 0 -> 1 */
                DEBUG_PRINT("No nodes have file %d, sending FINISH", file_idx);
                
                // Send FINISH aggressively
                send_finish(sockfd, clientaddr, new_seq);
                
                // Wait briefly, then send another FINISH
                usleep(50000);
                send_finish(sockfd, clientaddr, new_seq);
                
                if (current_ctx) {
                    current_ctx->waiting_ack = 0;
                }
            } else {
                DEBUG_PRINT("Found %d nodes with file %d", node_count, file_idx);
                
                // Send first RESPONSE immediately with extra redundancy
                struct node *current_node = send_return(sockfd, clientaddr, file_idx, file_nodes[0], SEQ0);
                
                // Update context to track state
                if (current_ctx) {
                    current_ctx->waiting_ack = 1;
                    current_ctx->clock = get_current_time();
                    current_ctx->noack_num = SEQ0;
                    current_ctx->noack_node = current_node;
                    current_ctx->file_idx = file_idx;
                    current_ctx->retry_count = 0;
                    DEBUG_PRINT("Updated context for file %d query", file_idx);
                    DEBUG_PRINT("[RDT3.0] INIT STATE: seq=%d, file=%d, first_node=%s, waiting_ack=1", 
                        SEQ0, file_idx, current_node ? current_node->name : "NULL");
                }
                
                // Immediately send redundant response (don't wait for timeout)
                usleep(50000); // Brief pause
                send_return(sockfd, clientaddr, file_idx, file_nodes[0], SEQ0);
            }
        }

        // Enhanced ACK handling with improved logic
        if (strncmp(buffer + parse_idx, ACK, strlen(ACK)) == 0) {
            DEBUG_PRINT("Received ACK with seq %d from %u:%hu", 
                       seq, clientaddr.sin_addr.s_addr, ntohs(clientaddr.sin_port));
            
            DEBUG_PRINT("[RDT3.0] ACK RECEIVED: seq=%d, expecting=%d, waiting_ack=%d, retry_count=%d", 
            seq, 
            current_ctx ? current_ctx->noack_num : -1, 
            current_ctx ? current_ctx->waiting_ack : 0,
            current_ctx ? current_ctx->retry_count : 0);
            char new_seq = 1 - seq; /* 1 -> 0, 0 -> 1 */
            
            if (current_ctx && current_ctx->waiting_ack) {
                if (current_ctx->noack_num == seq) {
                    DEBUG_PRINT("ACK matches expected seq %d", seq);
                    DEBUG_PRINT("[RDT3.0] STATE CHANGE: seq=%d matched, progress=%d/%d, next_seq=%d", 
                        seq, current_ctx->success_count, 
                        current_ctx->file_idx, new_seq);
                    current_ctx->clock = get_current_time();
                    current_ctx->noack_num = new_seq;
                    current_ctx->retry_count = 0; // Reset retry counter on successful ACK
                    current_ctx->success_count++;
                    
                    // Find next node with the file
                    struct node *next_node = current_ctx->noack_node ? current_ctx->noack_node->next : NULL;
                    
                    // If we have a next node, send a RESPONSE
                    if (next_node) {
                        struct node *found_next = NULL;
                        
                        // Find the next node that has the file
                        while (next_node != NULL) {
                            if ((next_node->register_flag & FILE_FLAG) && 
                                (next_node->file_map & (1U << (31 - current_ctx->file_idx)))) {
                                found_next = next_node;
                                break;
                            }
                            next_node = next_node->next;
                        }
                        
                        if (found_next) {
                            DEBUG_PRINT("Sending next RESPONSE for file %d to node %s", 
                                       current_ctx->file_idx, found_next->name);
                            current_ctx->noack_node = send_return(sockfd, clientaddr,
                                    current_ctx->file_idx, found_next, new_seq);
                            
                            // Double send for extra reliability
                            usleep(50000);
                            send_return(sockfd, clientaddr, current_ctx->file_idx, found_next, new_seq);
                        } else {
                            // No more nodes with the file
                            DEBUG_PRINT("No more nodes have the file, sending FINISH");
                            send_finish(sockfd, clientaddr, new_seq);
                            
                            // Wait briefly, then send another FINISH
                            usleep(50000);
                            send_finish(sockfd, clientaddr, new_seq);
                            
                            // One more for ultra reliability
                            usleep(50000);
                            send_finish(sockfd, clientaddr, new_seq);
                            
                            current_ctx->waiting_ack = 0;
                            current_ctx->noack_node = NULL;
                        }
                    } else {
                        DEBUG_PRINT("No more nodes have file %d, sending FINISH", current_ctx->file_idx);
                        
                        // 更激进的多次发送FINISH策略
                        for (int i = 0; i < 5; i++) {  // 增加到5次
                            send_finish(sockfd, clientaddr, new_seq);
                            usleep(10000);  // 减少间隔到10ms
                        }
                        
                        current_ctx->waiting_ack = 0;
                    }

                } else {
                    DEBUG_PRINT("Ignoring ACK with wrong seq: expected %d, got %d", 
                              current_ctx->noack_num, seq);
                }
            } else {
                DEBUG_PRINT("Received ACK but no context is waiting for it");

                // 更激进的多次发送FINISH策略
                for (int i = 0; i < 5; i++) {  // 增加到5次
                    send_finish(sockfd, clientaddr, new_seq);
                    usleep(10000);  // 减少间隔到10ms
                }
                
                current_ctx->waiting_ack = 0;
            }
        }

        bzero(send_buf, MAXMSG);
        parse_idx = 0;
        send_idx = 0;
        
        // Check for timeouts again before next iteration
        now = get_current_time();
        check_timeout(now, ctx_head, sockfd);
    }
    
    // Wait for chat thread to finish
    pthread_join(chat_thread, NULL);
    
    // Cleanup
    close(sockfd);
    DEBUG_PRINT("Server shutting down gracefully");
    return 0;
}