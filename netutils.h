#ifndef __NETUTILS_H_
#define __NETUTILS_H_

#include <string>
#include <vector>
#include <unistd.h>
#include <sys/socket.h>

// Operates on an active file descriptor.
void set_flags(int fd, int flags);
void clear_flags(int fd, int flags);

// Returns connected socket. Return value -1 indicates that
// connect() was attempted, and failed.
int socket_from_address(
    const std::string& hostname, int port_number,
    unsigned max_connect_time_s = 300);

// Waits for a client to accept
int get_client(int listening_socket);

// SO_REUSEADDR and SO_REUSEPORT
void set_reuse(int socket);

// connect(2) wih selectable timeout
int connect(
    int sockfd, const struct sockaddr *addr, socklen_t addrlen,
    unsigned maxwait_s);

// listen(2) on a collection of ports
class Listener
{
public:
    Listener(
        const std::string& hostname, const std::vector<int>& ports,
            int backlog);
    struct SocketInfo
    {
        int port_num;
        int socketFD;
    };
    SocketInfo get_client();
private:
    std::vector<SocketInfo> socket_infos;
    int maxfd;
    fd_set master_set;
};

#endif // __NETUTILS_H_
