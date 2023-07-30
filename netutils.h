#ifndef __NETUTILS_H_
#define __NETUTILS_H_

#include <string>
#include <unistd.h>
#include <sys/socket.h>

// Operates on an active file descriptor.
void set_flags(int fd, int flags);
void clear_flags(int fd, int flags);

// Returns connected or bound socket. Return value -1 indicates that
// connect() was attempted, and failed.
int socket_from_address(
    const std::string& hostname, int port_number, bool do_connect,
    unsigned max_connect_time_s = 300);

// Waits for a client to accept
int get_client(int listening_socket);

// Waits for two clients to accept
void get_two_clients(const int listening_socket[2], int client_socket[2]);

// SO_REUSEADDR and SO_REUSEPORT
void set_reuse(int socket);

// connect(2) wih selectable timeout
int connect(
    int sockfd, const struct sockaddr *addr, socklen_t addrlen,
    unsigned maxwait_s);

#endif // __NETUTILS_H_
