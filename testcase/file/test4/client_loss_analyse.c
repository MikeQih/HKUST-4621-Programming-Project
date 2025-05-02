#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdarg.h>  // Add this header for va_start, va_end

// 这个程序用于观察client_loss的行为模式，所有标记都输出到文件

// 获取当前时间戳字符串
char* get_timestamp() {
    static char timestamp[100];
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    sprintf(timestamp, "%04d-%02d-%02d %02d:%02d:%02d", 
            t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
            t->tm_hour, t->tm_min, t->tm_sec);
    return timestamp;
}

// 记录日志
void log_message(FILE *logfile, const char *format, ...) {
    va_list args;
    va_start(args, format);
    
    fprintf(logfile, "[%s] ", get_timestamp());
    vfprintf(logfile, format, args);
    fprintf(logfile, "\n");
    fflush(logfile);
    
    va_end(args);
}

// 使用统计模式分析client_loss程序的行为
void analyze_packet_loss_pattern(const char *logfile_path) {
    FILE *logfile = fopen(logfile_path, "w");
    if (!logfile) {
        perror("无法创建日志文件");
        exit(1);
    }

    log_message(logfile, "开始分析 client_loss 行为...");

    // 1. 设置UDP socket用于发送测试包
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t len = sizeof(client_addr);
    
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        log_message(logfile, "socket 创建失败");
        exit(1);
    }
    
    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(8888);
    
    if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        log_message(logfile, "bind 失败");
        exit(1);
    }
    
    log_message(logfile, "测试服务器启动在端口 8888");
    
    // 2. 发送一系列UDP包以分析client_loss的行为模式
    char buffer[1024];
    char send_buffer[1024];
    int total_sent = 500;  // 发送大量数据包以观察模式
    int received = 0;
    
    // 设置超时
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
    
    log_message(logfile, "开始发送 %d 个测试包...", total_sent);
    
    // 先接收一个数据包，获取client_loss的地址
    int n = recvfrom(sockfd, buffer, sizeof(buffer), 0, 
                    (struct sockaddr *)&client_addr, &len);
    
    if (n < 0) {
        log_message(logfile, "未收到初始数据包，无法获取client地址");
        exit(1);
    }
    
    log_message(logfile, "收到来自 %s:%d 的初始数据包", 
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
    
    // 开始测试不同类型的包
    
    // 测试1：发送一连串标准大小的包
    log_message(logfile, "\n----- 测试1：标准包大小 (100字节) -----");
    memset(send_buffer, 'A', 100);
    int lost = 0;
    
    for (int i = 0; i < 100; i++) {
        sprintf(send_buffer, "STD-PKT-%d", i);
        sendto(sockfd, send_buffer, strlen(send_buffer), 0, 
               (const struct sockaddr *)&client_addr, len);
        
        // 尝试接收响应
        n = recvfrom(sockfd, buffer, sizeof(buffer), 0, NULL, NULL);
        if (n < 0) {
            log_message(logfile, "包 %d: 未收到响应 (可能丢失)", i);
            lost++;
        } else {
            buffer[n] = '\0';
            log_message(logfile, "包 %d: 收到响应 '%s'", i, buffer);
        }
        usleep(50000);  // 50ms
    }
    
    log_message(logfile, "标准包丢包率: %.2f%% (%d/%d)", 
               (lost * 100.0 / 100), lost, 100);
    
    // 测试2：发送不同大小的包
    log_message(logfile, "\n----- 测试2：不同包大小 -----");
    int sizes[] = {10, 50, 100, 200, 500, 1000};
    
    for (int i = 0; i < sizeof(sizes)/sizeof(sizes[0]); i++) {
        memset(send_buffer, 'B', sizes[i]);
        send_buffer[sizes[i]] = '\0';
        
        int test_count = 20;
        int size_lost = 0;
        
        log_message(logfile, "测试 %d 字节大小的包:", sizes[i]);
        
        for (int j = 0; j < test_count; j++) {
            sprintf(send_buffer, "SIZE-%d-%d", sizes[i], j);
            sendto(sockfd, send_buffer, strlen(send_buffer), 0, 
                   (const struct sockaddr *)&client_addr, len);
            
            n = recvfrom(sockfd, buffer, sizeof(buffer), 0, NULL, NULL);
            if (n < 0) {
                size_lost++;
            } else {
                buffer[n] = '\0';
            }
            usleep(50000);  // 50ms
        }
        
        log_message(logfile, "  %d 字节包丢包率: %.2f%% (%d/%d)", 
                   sizes[i], (size_lost * 100.0 / test_count), size_lost, test_count);
    }
    
    // 测试3：测试发送间隔的影响
    log_message(logfile, "\n----- 测试3：不同发送间隔 -----");
    int delays[] = {10000, 20000, 50000, 100000, 200000};  // 微秒
    
    for (int i = 0; i < sizeof(delays)/sizeof(delays[0]); i++) {
        int test_count = 20;
        int delay_lost = 0;
        
        log_message(logfile, "测试 %d 微秒间隔:", delays[i]);
        
        for (int j = 0; j < test_count; j++) {
            sprintf(send_buffer, "DELAY-%d-%d", delays[i], j);
            sendto(sockfd, send_buffer, strlen(send_buffer), 0, 
                   (const struct sockaddr *)&client_addr, len);
            
            n = recvfrom(sockfd, buffer, sizeof(buffer), 0, NULL, NULL);
            if (n < 0) {
                delay_lost++;
            } else {
                buffer[n] = '\0';
            }
            usleep(delays[i]);
        }
        
        log_message(logfile, "  %d 微秒间隔丢包率: %.2f%% (%d/%d)", 
                   delays[i], (delay_lost * 100.0 / test_count), delay_lost, test_count);
    }
    
    // 测试4：测试突发发送
    log_message(logfile, "\n----- 测试4：突发发送 -----");
    int burst_sizes[] = {2, 3, 5, 10};
    
    for (int i = 0; i < sizeof(burst_sizes)/sizeof(burst_sizes[0]); i++) {
        int test_count = 10;
        int burst_lost = 0;
        int total_burst_pkts = 0;
        
        log_message(logfile, "测试 %d 包的突发:", burst_sizes[i]);
        
        for (int j = 0; j < test_count; j++) {
            log_message(logfile, "  突发 %d:", j);
            
            for (int k = 0; k < burst_sizes[i]; k++) {
                sprintf(send_buffer, "BURST-%d-%d-%d", burst_sizes[i], j, k);
                sendto(sockfd, send_buffer, strlen(send_buffer), 0, 
                       (const struct sockaddr *)&client_addr, len);
                total_burst_pkts++;
                
                n = recvfrom(sockfd, buffer, sizeof(buffer), 0, NULL, NULL);
                if (n < 0) {
                    burst_lost++;
                    log_message(logfile, "    包 %d: 丢失", k);
                } else {
                    buffer[n] = '\0';
                    log_message(logfile, "    包 %d: 收到", k);
                }
                usleep(1000);  // 1ms之内的突发
            }
            
            usleep(100000);  // 突发之间间隔100ms
        }
        
        log_message(logfile, "  %d 包突发丢包率: %.2f%% (%d/%d)", 
                   burst_sizes[i], (burst_lost * 100.0 / total_burst_pkts), 
                   burst_lost, total_burst_pkts);
    }
    
    // 测试5：连续接收能力
    log_message(logfile, "\n----- 测试5：连续接收能力 -----");
    int successive_count = 30;
    int successive_lost = 0;
    
    for (int i = 0; i < successive_count; i++) {
        // 发送标记有序号的数据包
        sprintf(send_buffer, "SUCCESSIVE-%d", i);
        sendto(sockfd, send_buffer, strlen(send_buffer), 0, 
               (const struct sockaddr *)&client_addr, len);
        
        // 尝试接收响应 
        n = recvfrom(sockfd, buffer, sizeof(buffer), 0, NULL, NULL);
        if (n < 0) {
            log_message(logfile, "连续包 %d: 丢失", i);
            successive_lost++;
        } else {
            buffer[n] = '\0';
            log_message(logfile, "连续包 %d: 收到回复", i);
        }
        
        // 无等待，尽快发送下一个数据包
    }
    
    log_message(logfile, "连续包丢包率: %.2f%% (%d/%d)", 
               (successive_lost * 100.0 / successive_count), successive_lost, successive_count);
    
    // 分析结果
    log_message(logfile, "\n===== 分析结果 =====");
    log_message(logfile, "测试完成。请检查日志了解client_loss的行为模式。");
    
    fclose(logfile);
}

int main() {
    analyze_packet_loss_pattern("client_loss_analysis.log");
    return 0;
}