#ifndef __NETUTILS_H_
#define __NETUTILS_H_

#include <string>
#include <vector>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/poll.h>

class NetutilsException
{
public:
    NetutilsException(const std::string& str) : strng(str) { }
    std::string strng;
};

// Operates on an active file descriptor.
void set_flags(int fd, int flags);
void clear_flags(int fd, int flags);

// Sets SO_REUSEADDR and SO_REUSEPORT
void set_reuse(int socket);

// Sets SO_LINGER to {0, 0}
void set_nolinger(int socket);

// Calls shutdown() and close()
void graceful_close(int socketFD);

// Returns connected socket. Return value -1 indicates that
// connect() was attempted, and failed.
int socket_from_address(
    const std::string& hostname, int port_number,
    unsigned max_connect_time_s = 300);

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
    ~Listener();
    struct SocketInfo
    {
        int port_num;
        int socketFD;
    };
    SocketInfo get_client();
private:
    size_t num_ports;
    int* listening_ports;
    pollfd* pfds;
};

#endif // __NETUTILS_H_
