# Multithreaded TCP Chat Server

A multithreaded client-server chat application written in C using POSIX sockets, TCP/IP networking, and pthreads. The server supports multiple concurrent users, dynamic chat rooms, colored usernames, real time messaging, and peer to peer file transfers while ensuring thread safe communication through mutex synchronization.

---

## Features

- Multi-threaded server capable of handling many clients simultaneously
- Dynamic chat room creation and room selection
- Real-time messaging within chat rooms
- Username registration for each client
- Color-coded usernames for improved readability
- Peer-to-peer file transfer between users in the same room
- Thread-safe shared data structures using POSIX mutexes
- Automatic join/leave notifications
- Server-side room and participant tracking
- TCP socket communication over IPv4

---

## Technologies Used

- C
- POSIX Threads (pthreads)
- TCP/IP Socket Programming
- Linux
- GCC
- POSIX Networking APIs

---

## Project Architecture

```
                        +----------------------+
                        |      Server          |
                        |  (main_server.c)     |
                        +----------+-----------+
                                   |
              -----------------------------------------
              |                 |                     |
        Client Thread      Client Thread       Client Thread
              |                 |                     |
           Client A          Client B            Client C
               \                |                /
                \               |               /
                 +-----------------------------+
                 |        Chat Room #1         |
                 +-----------------------------+
```

Each connected client is assigned its own thread, allowing multiple users to communicate simultaneously without blocking one another.

---

## Project Structure

```
multithreaded-chat-server/
│
├── README.md
├── LICENSE
├── Makefile
│
├── src
│   ├── main_server.c
│   └── main_client.c
│
├── screenshots
│   ├── server.png
│   ├── chat.png
│   └── file-transfer.png
│
└── docs
```

---

## How It Works

### Server

The server:

- Listens for incoming TCP connections on port **1004**
- Creates a dedicated thread for every connected client
- Maintains multiple chat rooms
- Broadcasts messages only within the sender's room
- Handles room creation and room joining
- Synchronizes shared resources using mutexes
- Coordinates peer-to-peer file transfers

---

### Client

The client:

- Connects to the server
- Allows users to join or create chat rooms
- Registers a username
- Sends chat messages
- Receives messages asynchronously using a separate receiver thread
- Supports sending files to another user

---

## Thread Synchronization

Because multiple client threads access shared room data simultaneously, the server uses **POSIX mutexes** to prevent race conditions.

Protected shared resources include:

- Chat room list
- Client list
- Room membership
- Color assignment
- Broadcast operations

---

## File Transfer

Users can send files directly to another user in the same chat room.

### Workflow

```
Sender
   │
   │ SEND username filename
   ▼
Server
   │
   │ Notify recipient
   ▼
Recipient
   │
Accept? (Y/N)
   │
   ▼
Transfer begins
```

If the recipient accepts, the server streams the file over the existing TCP connection.

---

## Skills Demonstrated

- Concurrent Programming
- Multithreading
- POSIX Threads
- Socket Programming
- TCP/IP Networking
- Linux Systems Programming
- Client-Server Architecture
- Synchronization with Mutexes
- Dynamic Memory Management
- File I/O
- Data Structures (Linked Lists)

---

## Future Improvements

- End-to-end encryption
- Private messaging
- User authentication
- File transfer progress indicators
- Chat history persistence
- Support for multiple simultaneous file transfers
- Cross-platform compatibility
- GUI client

---

## Author

**Abishekk Kailashram Krishna**

Linkedin: https://www.linkedin.com/in/abishekk-krishna/

---
