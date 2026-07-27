// main_server.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>

#define PORT_NUM 1004
#define MAX_ROOMS 100
#define MAX_CLIENTS_PER_ROOM 1000
#define BUF_SIZE 1024
#define FILE_BUF 4096

//  error function
void error(const char *msg) { perror(msg); exit(1); }

//color codes for usernames 
const char* COLOR_CODES[] = {
    "\x1b[31m","\x1b[32m","\x1b[33m","\x1b[34m",
    "\x1b[35m","\x1b[36m","\x1b[37m","\x1b[91m",
    "\x1b[92m","\x1b[93m"
};
const char* COLOR_RESET = "\x1b[0m";
const int COLOR_COUNT = sizeof(COLOR_CODES)/sizeof(COLOR_CODES[0]);


// Linked-list node representing one client
typedef struct Client {
    int sockfd;                   
    int room_id;                  
    char username[64];            // chosen username
    char ipstr[INET_ADDRSTRLEN];  
    int color_idx;                
    struct Client* next;          
} Client;

// Chat room structure
typedef struct Room {
    int id;             
    Client* clients;     
    int client_count;    
    int next_color;      
} Room;

Room rooms[MAX_ROOMS];
int room_count = 0;

// Mutex to protect room and client lists
pthread_mutex_t rooms_lock = PTHREAD_MUTEX_INITIALIZER;



// Find a room by ID
Room* find_room(int room_id) {
    for (int i = 0; i < room_count; i++)
        if (rooms[i].id == room_id) return &rooms[i];
    return NULL;
}

// Find a client by username inside a specific room
Client* find_client_in_room(Room* r, const char* username) {
    Client* cur = r->clients;
    while (cur) {
        if (strcmp(cur->username, username) == 0)
            return cur;
        cur = cur->next;
    }
    return NULL;
}

// Create a new room 
int create_room_locked() {
    if (room_count >= MAX_ROOMS) return -1;
    int id = (room_count == 0) ? 1 : (rooms[room_count - 1].id + 1);

    rooms[room_count].id = id;
    rooms[room_count].clients = NULL;
    rooms[room_count].client_count = 0;
    rooms[room_count].next_color = 0;
    room_count++;

    return id;
}

// Remove a client 
void remove_client_from_room_locked(Client* c) {
    Room* r = find_room(c->room_id);
    if (!r) return;

    Client** cur = &r->clients;
    while (*cur) {
        if (*cur == c) {
            *cur = c->next;
            r->client_count--;
            c->next = NULL;
            return;
        }
        cur = &((*cur)->next);
    }
}

// Assign the next available color to the user
int assign_color_idx_locked(Room* r) {
    int start = r->next_color % COLOR_COUNT;
    int used[COLOR_COUNT];
    memset(used, 0, sizeof(used));

    // Mark colors 
    Client* cur = r->clients;
    while (cur) {
        if (cur->color_idx >= 0 && cur->color_idx < COLOR_COUNT)
            used[cur->color_idx] = 1;
        cur = cur->next;
    }

    // Find a free color
    for (int i = 0; i < COLOR_COUNT; i++) {
        int idx = (start + i) % COLOR_COUNT;
        if (!used[idx]) {
            r->next_color = (idx + 1) % COLOR_COUNT;
            return idx;
        }
    }

    return start;
}

// Add a client to a room
void add_client_to_room_locked(Room* r, Client* c) {
    c->room_id = r->id;
    c->color_idx = assign_color_idx_locked(r);
    c->next = r->clients;
    r->clients = c;
    r->client_count++;
}

// Send a message to all clients in a room
void broadcast_to_room(Room* r, int exclude_sockfd, const char* msg) {
    Client* cur = r->clients;
    while (cur) {
        if (cur->sockfd != exclude_sockfd)
            send(cur->sockfd, msg, strlen(msg), 0);
        cur = cur->next;
    }
}

// Debug print of all rooms + clients
void server_print_client_list_locked() {
    printf("=== Server: current rooms and participants ===\n");
    for (int i = 0; i < room_count; i++) {
        Room* r = &rooms[i];
        printf("Room %d: %d people\n", r->id, r->client_count);

        Client* c = r->clients;
        while (c) {
            printf("  - %s (%s)\n",
                   c->username[0] ? c->username : "(no-name)",
                   c->ipstr);
            c = c->next;
        }
    }
    fflush(stdout);
}



typedef struct {
    Client* sender;
    Client* receiver;
    char filename[256];
} FileTransferArg;

// Handles a file transfer 
void* file_transfer_thread(void* arg) {
    FileTransferArg* f = (FileTransferArg*)arg;
    char buf[FILE_BUF];

    FILE* fp = fopen(f->filename, "rb");
    if (!fp) {
        char msg[128];
        snprintf(msg, sizeof(msg), "ERR cannot open file\n");
        send(f->sender->sockfd, msg, strlen(msg), 0);
        free(f);
        return NULL;
    }

    // Send notification 
    char notif[BUF_SIZE];
    snprintf(notif, sizeof(notif),
             "%s wants to send you file %s. Accept? (Y/N)\n",
             f->sender->username, f->filename);
    send(f->receiver->sockfd, notif, strlen(notif), 0);

    // Wait for response
    int n = recv(f->receiver->sockfd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) { fclose(fp); free(f); return NULL; }
    buf[n] = '\0';
    if (buf[n-1] == '\n') buf[n-1] = '\0';

    // Abort if it is denied 
    if (buf[0] != 'Y' && buf[0] != 'y') {
        fclose(fp);
        free(f);
        return NULL;
    }

    // Transfer file 
    while (1) {
        size_t r = fread(buf, 1, sizeof(buf), fp);
        if (r > 0)
            send(f->receiver->sockfd, buf, r, 0);
        if (r < sizeof(buf)) break;
    }

    fclose(fp);
    char done[] = "File transfer complete.\n";
    send(f->receiver->sockfd, done, strlen(done), 0);

    free(f);
    return NULL;
}



typedef struct {
    int sockfd;
    struct sockaddr_in addr;
} ThreadArg;

void* client_thread(void* v) {
    pthread_detach(pthread_self());

    ThreadArg* targ = (ThreadArg*)v;
    int sockfd = targ->sockfd;
    struct sockaddr_in addr = targ->addr;
    free(targ);

    // Convert IP address to string
    char ipstr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(addr.sin_addr), ipstr, sizeof(ipstr));

    // Allocate and initialize client structure
    Client* c = (Client*)malloc(sizeof(Client));
    memset(c, 0, sizeof(Client));
    c->sockfd = sockfd;
    c->room_id = -1;

    strncpy(c->ipstr, ipstr, sizeof(c->ipstr)-1);

    char buf[BUF_SIZE];

   
    // Receive first command
    int n = recv(sockfd, buf, sizeof(buf)-1, 0);
    if (n <= 0) { close(sockfd); free(c); return NULL; }
    buf[n] = '\0';
    if (buf[n-1] == '\n') buf[n-1] = '\0';

    pthread_mutex_lock(&rooms_lock);

    int chosen_room = -1;

    // Create new room
    if (strncmp(buf, "NEW", 3) == 0) {
        chosen_room = create_room_locked();
    }
    // Join existing room
    else if (strncmp(buf, "JOIN", 4) == 0) {
        chosen_room = atoi(buf + 5);
        if (!find_room(chosen_room)) {
            char err[] = "ERR room-not-found\n";
            send(sockfd, err, strlen(err), 0);
            pthread_mutex_unlock(&rooms_lock);
            close(sockfd);
            free(c);
            return NULL;
        }
    }
    // List rooms then wait for new client choice
    else if (strncmp(buf, "LIST", 4) == 0) {

        // Send all available rooms
        char listbuf[BUF_SIZE]; 
        int off = 0;
        if (room_count == 0) create_room_locked();

        for (int i = 0; i < room_count; i++) {
            Room* r = &rooms[i];
            off += snprintf(listbuf + off, sizeof(listbuf) - off,
                "Room %d: %d %s\n",
                r->id, r->client_count,
                (r->client_count == 1 ? "person" : "people"));
        }

        char reply[BUF_SIZE];
        snprintf(reply, sizeof(reply),
            "Server says following options are available:\n"
            "%sChoose the room number or type [new] to create a new room:\n",
            listbuf);
        send(sockfd, reply, strlen(reply), 0);

        // Wait for the second handshake from user
        int n2 = recv(sockfd, buf, sizeof(buf)-1, 0);
        if (n2 <= 0) {
            pthread_mutex_unlock(&rooms_lock);
            close(sockfd);
            free(c);
            return NULL;
        }
        buf[n2] = '\0';
        if (buf[n2-1] == '\n') buf[n2-1] = '\0';

        if (strncmp(buf, "NEW", 3) == 0) {
            chosen_room = create_room_locked();
        }
        else if (strncmp(buf, "JOIN", 4) == 0) {
            chosen_room = atoi(buf + 5);
            if (!find_room(chosen_room)) {
                char err[] = "ERR room-not-found\n";
                send(sockfd, err, strlen(err), 0);
                pthread_mutex_unlock(&rooms_lock);
                close(sockfd);
                free(c);
                return NULL;
            }
        }
        else {
            char err[] = "ERR bad-handshake\n";
            send(sockfd, err, strlen(err), 0);
            pthread_mutex_unlock(&rooms_lock);
            close(sockfd);
            free(c);
            return NULL;
        }
    }
    else {
        char err[] = "ERR bad-handshake\n";
        send(sockfd, err, strlen(err), 0);
        pthread_mutex_unlock(&rooms_lock);
        close(sockfd);
        free(c);
        return NULL;
    }

    // Add client to chosen room
    Room* r = find_room(chosen_room);
    add_client_to_room_locked(r, c);
    char ok[128];
    snprintf(ok, sizeof(ok), "ROOM %d\n", r->id);
    send(sockfd, ok, strlen(ok), 0);

    pthread_mutex_unlock(&rooms_lock);

    // set username
    n = recv(sockfd, buf, sizeof(buf)-1, 0);
    if (n <= 0) {
        pthread_mutex_lock(&rooms_lock);
        remove_client_from_room_locked(c);
        server_print_client_list_locked();
        pthread_mutex_unlock(&rooms_lock);
        close(sockfd);
        free(c);
        return NULL;
    }
    buf[n] = '\0';
    if (buf[n-1] == '\n') buf[n-1] = '\0';

    if (strncmp(buf, "NAME ", 5) == 0)
        strncpy(c->username, buf + 5, sizeof(c->username)-1);
    else
        strncpy(c->username, "Anon", sizeof(c->username)-1);

    // Announce user join
    pthread_mutex_lock(&rooms_lock);
    char notice[BUF_SIZE];
    snprintf(
        notice, sizeof(notice),
        "%s%s (%s) joined the chat room!%s\n",
        COLOR_CODES[c->color_idx], c->username, c->ipstr, COLOR_RESET
    );
    broadcast_to_room(r, -1, notice);
    server_print_client_list_locked();
    pthread_mutex_unlock(&rooms_lock);

    //main loop
    while (1) {
        int rn = recv(sockfd, buf, sizeof(buf)-1, 0);
        if (rn <= 0) break;

        buf[rn] = '\0';
        if (buf[rn-1] == '\n') buf[rn-1] = '\0';

        // Chat message
        if (strncmp(buf, "MSG ", 4) == 0) {
            char out[BUF_SIZE];
            snprintf(out, sizeof(out),
                     "%s[%s]%s %s\n",
                     COLOR_CODES[c->color_idx],
                     c->username,
                     COLOR_RESET,
                     buf+4);

            pthread_mutex_lock(&rooms_lock);
            broadcast_to_room(r, c->sockfd, out);
            pthread_mutex_unlock(&rooms_lock);
        }

        // File transfer command: SEND 
        else if (strncmp(buf, "SEND ", 5) == 0) {
            char recipient[64], filename[256];

            if (sscanf(buf+5, "%s %s", recipient, filename) == 2) {
                pthread_mutex_lock(&rooms_lock);
                Client* recv_c = find_client_in_room(r, recipient);
                pthread_mutex_unlock(&rooms_lock);

                if (recv_c) {
                    FileTransferArg* f = malloc(sizeof(FileTransferArg));
                    f->sender = c;
                    f->receiver = recv_c;
                    strncpy(f->filename, filename, sizeof(f->filename)-1);

                    pthread_t tid;
                    pthread_create(&tid, NULL, file_transfer_thread, f);
                } else {
                    char err[] = "ERR recipient not found in room\n";
                    send(c->sockfd, err, strlen(err), 0);
                }
            }
        }

        // Quit
        else if (strncmp(buf, "QUIT", 4) == 0) break;
    }

    //cleanup
    pthread_mutex_lock(&rooms_lock);
    snprintf(
        notice, sizeof(notice),
        "%s%s (%s) left the room!%s\n",
        COLOR_CODES[c->color_idx], c->username, c->ipstr, COLOR_RESET
    );
    remove_client_from_room_locked(c);
    broadcast_to_room(r, -1, notice);
    server_print_client_list_locked();
    pthread_mutex_unlock(&rooms_lock);

    close(sockfd);
    free(c);
    return NULL;
}


//main
int main() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) error("ERROR opening socket");

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(PORT_NUM);

    if (bind(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR on binding");
    if (listen(sockfd, 20) < 0)
        error("listen failed");

    printf("main_server: listening on port %d\n", PORT_NUM);
    fflush(stdout);

    // Always create at least one room
    pthread_mutex_lock(&rooms_lock);
    if (room_count == 0) create_room_locked();
    pthread_mutex_unlock(&rooms_lock);

    // Accept incoming connections forever
    while (1) {
        struct sockaddr_in cli_addr;
        socklen_t clen = sizeof(cli_addr);

        int newsockfd = accept(sockfd, (struct sockaddr*)&cli_addr, &clen);
        if (newsockfd < 0) {
            perror("accept");
            continue;
        }

        printf("Connected: %s\n", inet_ntoa(cli_addr.sin_addr));
        fflush(stdout);

        ThreadArg* targ = malloc(sizeof(ThreadArg));
        targ->sockfd = newsockfd;
        targ->addr = cli_addr;

        pthread_t tid;
        pthread_create(&tid, NULL, client_thread, targ);
    }

    close(sockfd);
    return 0;
}
