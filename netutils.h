#ifndef __NETUTILS_H_
#define __NETUTILS_H_

#include <cstddef>
#include <list>
#include <string>
#include <vector>
#include <sys/socket.h>
#include <sys/poll.h>

struct NetutilsException
{
    NetutilsException(const std::string& str) : strng(str) { }
    std::string strng;
};

// Sets SO_REUSEADDR and SO_REUSEPORT
void set_reuse(int socket);

// Returns connected socket. Return value -1 indicates that
// connect() was attempted, and failed.
int socket_from_address(
    unsigned client_num, const std::string& hostname, int port_number,
    int max_connect_time_ms = 300*1000);

// connect(2) wih selectable timeout
int connect(
    int sockfd, const struct sockaddr *addr, socklen_t addrlen, int maxwait_ms);

// listen(2) on a collection of ports
class Listener
{
public:
    Listener(
        const std::string& hostname, const std::vector<int>& ports,
        int backlog);
    ~Listener();
    Listener(Listener&& other);
    Listener& operator=(Listener&& other);
    struct SocketInfo
    {
        int port_num;
        int socketFD;
    };
    SocketInfo get_client(unsigned client_num);
    Listener() = delete;
    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;
private:
    size_t num_ports;
    int* listening_ports;
    pollfd* pfds;
    std::list<SocketInfo> accepted_queue;
};

#endif // __NETUTILS_H_
