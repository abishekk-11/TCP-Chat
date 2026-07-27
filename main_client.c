// main_client.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT_NUM 1004     
#define BUF_SIZE 1024    
#define FILE_BUF 4096    

void error(const char* msg){ perror(msg); exit(0); }

// Small struct to pass socket descriptor into threads
typedef struct { int sockfd; } ThreadArgs;

// recieve threads
void* recv_thread(void* arg){
    int sockfd = ((ThreadArgs*)arg)->sockfd;
    free(arg); // Thread no longer needs pointer

    char buf[BUF_SIZE];
    while(1){
        // Read from server
        int n = recv(sockfd, buf, sizeof(buf)-1, 0);
        if(n <= 0) break; // Server closed or error

        buf[n] = '\0';

        // server ask user to accept
        if(strstr(buf, "Accept? (Y/N)") != NULL){
            printf("%s", buf);
            fflush(stdout);

            // Read Y/N 
            char ans[8];
            fgets(ans, sizeof(ans), stdin);
            send(sockfd, ans, strlen(ans), 0);
            continue;
        }

        // Print message
        printf("%s", buf);
        fflush(stdout);
    }
    return NULL;
}

// send thread
void* send_thread(void* arg){
    int sockfd = ((ThreadArgs*)arg)->sockfd;
    free(arg);

    char line[BUF_SIZE];
    while(1){
        // Read user input
        if(!fgets(line, sizeof(line), stdin)){
            // EOF or input closed
            send(sockfd, "QUIT\n", 5, 0);
            break;
        }

        // Strip newline
        size_t len = strlen(line);
        if(len > 0 && line[len-1] == '\n'){
            line[len-1] = '\0';
            len--;
        }

        // Empty line means quit
        if(len == 0){
            send(sockfd, "QUIT\n", 5, 0);
            break;
        }

        // File transfer command
        if(strncmp(line, "SEND ", 5) == 0){
            // Send command as-is to server
            send(sockfd, line, strlen(line), 0);
            // Server will follow up with file-transfer protocol
            continue;
        }

        // Normal chat message
        char out[BUF_SIZE];
        snprintf(out, sizeof(out), "MSG %s\n", line);
        send(sockfd, out, strlen(out), 0);
    }
    return NULL;
}

//main
int main(int argc, char* argv[]){
    if(argc < 2) error("Specify hostname (IP address)");

    // Create TCP socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0) error("ERROR opening socket");

    // Prepare server address structure
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;

    // Convert IP string to binary form
    if(inet_pton(AF_INET, argv[1], &serv_addr.sin_addr) != 1)
        error("Invalid IP");

    serv_addr.sin_port = htons(PORT_NUM);

    printf("Connecting to %s...\n", argv[1]);

    // Connect to the server
    if(connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR connecting");

    char buf[BUF_SIZE];

    //room selection logic
    if(argc >= 3){
        // Commandline argument specifies "new" or a room name
        if(strcmp(argv[2], "new") == 0)
            send(sockfd, "NEW\n", 4, 0);
        else{
            char tmp[128];
            snprintf(tmp, sizeof(tmp), "JOIN %s\n", argv[2]);
            send(sockfd, tmp, strlen(tmp), 0);
        }
    } else {
        // Ask server for room list
        send(sockfd, "LIST\n", 5, 0);

        // Receive available rooms
        int n = recv(sockfd, buf, sizeof(buf)-1, 0);
        if(n <= 0){
            close(sockfd);
            return 0;
        }

        buf[n] = '\0';
        printf("%s", buf);
        fflush(stdout);

        // User selects a room
        char choice[128];
        fgets(choice, sizeof(choice), stdin);

        // Strip newline
        size_t l = strlen(choice);
        if(l > 0 && choice[l-1] == '\n')
            choice[l-1] = '\0';

        // Interpret user selection
        if(strcmp(choice, "new") == 0 || strcmp(choice, "NEW") == 0)
            send(sockfd, "NEW\n", 4, 0);
        else {
            char tmp[128];
            snprintf(tmp, sizeof(tmp), "JOIN %s\n", choice);
            send(sockfd, tmp, strlen(tmp), 0);
        }
    }

    //username setup
    printf("Type your user name: ");
    fflush(stdout);

    char name[64];
    fgets(name, sizeof(name), stdin);

// Remove trailing newline
    size_t l = strlen(name);
    if(l > 0 && name[l-1] == '\n')
        name[l-1] = '\0';

    char out[BUF_SIZE];
    snprintf(out, sizeof(out), "NAME %s\n", name);
    send(sockfd, out, strlen(out), 0);

    //start threads
    ThreadArgs* rarg = malloc(sizeof(ThreadArgs));
    rarg->sockfd = sockfd;

  // Receiver thread handle
    pthread_t rt;
    pthread_create(&rt, NULL, recv_thread, rarg);

    ThreadArgs* sarg = malloc(sizeof(ThreadArgs));
    sarg->sockfd = sockfd;

// Sender thread handle
    pthread_t st;
    pthread_create(&st, NULL, send_thread, sarg);

    // Wait for both threads to finish
    pthread_join(rt, NULL);
    pthread_join(st, NULL);

    close(sockfd);
    return 0;
}
