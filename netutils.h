#ifndef __NETUTILS_H_
#define __NETUTILS_H_

#include <string>
#include <unistd.h>
#include <sys/socket.h>

// Operates on an active file descriptor.
void set_flags(int fd, int flags);

// Returns connected or bound socket.
int socket_from_address(
    const std::string& hostname, int port_number, bool do_connect);

// Waits for a client
int get_client(int listening_socket);

// Waits for two clients.
void get_two_clients(const int listening_socket[2], int client_socket[2]);

// SO_REUSEADDR and SO_REUSEPORT
void set_reuse(int socket);

#endif // __NETUTILS_H_
