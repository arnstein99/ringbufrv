#include "netutils.h"
#include "miscutils.h"

#include <iostream>
#include <thread>
#include <chrono>
using namespace std::chrono_literals;
#include <string.h>
#include <fcntl.h>
#include <netdb.h>
#include <arpa/inet.h>

void set_flags(int fd, int flags)
{
    int oldflags = fcntl(fd, F_GETFL, 0);
    oldflags |= flags;
    ZEROCHECK("fcntl", fcntl(fd, F_SETFL, oldflags));
}

void clear_flags(int fd, int flags)
{
    int oldflags = fcntl(fd, F_GETFL, 0);
    oldflags &= (~flags);
    ZEROCHECK("fcntl", fcntl(fd, F_SETFL, oldflags));
}

int socket_from_address(
    const std::string& hostname, int port_number,
    unsigned max_connecttime_s)
{
    // Create socket
    int socketFD;
    NEGCHECK("socket", (socketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)));

    // Process host name
    struct sockaddr_in serveraddr;
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(port_number);

    struct hostent* server = gethostbyname(hostname.c_str());
    if (server == nullptr)
    {
        std::string str = "gethostbyname(";
        str += hostname;
        str += ") : ";
        str += strerror(h_errno);
        NetutilsException r(str);
        throw(r);
    }
    bcopy((char *)server->h_addr,
    (char *)&serveraddr.sin_addr.s_addr, server->h_length);

    // Connect to server
    if (connect(
        socketFD, (struct sockaddr*)(&serveraddr), sizeof(serveraddr),
        max_connecttime_s) < 0)
    {
        close(socketFD);
        return -1;
    }
#if (VERBOSE >= 1)
    std::cerr << my_time() << " connected " <<
        inet_ntoa(serveraddr.sin_addr) << ":" << port_number << std::endl;
#endif
#if (VERBOSE >= 2)
        std::cerr << my_time() << "     using FD " << socketFD <<
            std::endl;
#endif

    return socketFD;
}

void set_reuse(int socket)
{
    int reuse = 1;
    NEGCHECK("setsockopt",
        setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)));
#ifdef SO_REUSEPORT
    NEGCHECK("setsockopt",
        setsockopt(socket, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)));
#endif
}
void set_nolinger(int socket)
{
    struct linger ling {0, 0};
    NEGCHECK("setsockopt",
        setsockopt(socket, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling)));
}

void graceful_close(int socketFD)
{
    if (shutdown(socketFD, SHUT_RDWR) != 0)
    {
#if (VERBOSE >= 2)
        std::cerr << my_time() << " shutdown(" << socketFD << ") : " <<
            strerror(errno) << std::endl;
#endif
    }
    NEGCHECK("close", close(socketFD));
}

int connect(
    int sockfd, const struct sockaddr *addr, socklen_t addrlen,
    unsigned maxwait_s)
{
    set_flags(sockfd, O_NONBLOCK);
    int retval = connect(sockfd, addr, addrlen);
    if (retval < 0)
    {
        if (errno == EINPROGRESS)
        {
            pollfd pfd;
            pfd.fd = sockfd;
            pfd.events = POLLOUT;
            NEGCHECK("poll", (retval = poll(&pfd, 1, 1000 * maxwait_s)));
            switch (retval)
            {
            case 0:
                // Timeout
                retval = -1;
                break;
            case 1:
                // Success, maybe.
                if (pfd.revents & POLLOUT)
                {
                    retval = 0;
                }
                else
                {
                    std::cerr << "Unexpected revents = " << pfd.revents <<
                        "from connect()/poll()" << std::endl;
                    exit(1);
                }
                break;
            default:
                std::cerr << "connect() : poll() returns " <<
                    retval << std::endl;
                exit(1);
                break;
            }
        }
    }
    return retval;
}

////////////////////////////
// Listener class methods //
////////////////////////////

Listener::Listener(const std::string& hostname, const std::vector<int>& ports,
    int backlog)
{
    num_ports = ports.size();
    listening_ports = new int[num_ports];
    pfds = new pollfd[num_ports];
    memset(pfds, 0, num_ports * sizeof(pollfd));
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    if (hostname == "")
    {
        sa.sin_addr.s_addr = htonl (INADDR_ANY);
    }
    else
    {
        if ((sa.sin_addr.s_addr = inet_addr (hostname.c_str())) ==
            INADDR_NONE)
        {
            std::string str = "inet_addr";
            str += "(";
            str += hostname;
            str += ") fails";
            NetutilsException r(str);
            throw(r);
        }
    }
    int optval = 1;
    size_t index = 0;
    for (auto port_num : ports)
    {
        int socketFD;
        NEGCHECK(
            "socket", socketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP));
        pfds[index].fd = socketFD;
        pfds[index].events = POLLIN;
        listening_ports[index] = port_num;

        set_reuse(socketFD);
        set_flags(socketFD, O_NONBLOCK);
        NEGCHECK("setsockopt",
            setsockopt(socketFD, SOL_SOCKET, SO_KEEPALIVE, &optval,
            sizeof(optval)));
        sa.sin_port = htons ((uint16_t)port_num);
        NEGCHECK("bind",
            bind(socketFD, (struct sockaddr *)(&sa), (socklen_t)sizeof (sa)));
        NEGCHECK("listen",  listen(socketFD, backlog));
    }
}

Listener::~Listener()
{
    delete[] pfds;
    delete[] listening_ports;
}

Listener::SocketInfo Listener::get_client()
{
    int poll_return;
    size_t index;
    do
    {
        NEGCHECK("poll", (poll_return = poll(pfds, num_ports, -1)));
        if (poll_return == 0)
        {
            // timeout
            std::cerr << "Listener::get_client() : poll() returns 0" <<
                std::endl;
            exit(1);
        }
        for (index = 0 ; index < num_ports ; ++index)
        {
            if (pfds[index].revents & POLLIN)
            {
                // Debug code
                std::cerr << "poll()/listen: revents = " <<
                    pfds[index].revents << std::endl;
                break;
            }
        }
    } while (index >= num_ports);
    SocketInfo return_info;
    return_info.port_num = listening_ports[index];

    struct sockaddr_in addr;
    socklen_t addrlen = (socklen_t)sizeof(addr);
    NEGCHECK("accept", (return_info.socketFD = accept(
        pfds[index].fd, (struct sockaddr*)(&addr), &addrlen)));

#if (VERBOSE >= 1)
    std::cerr << my_time() << " accepted " << inet_ntoa(addr.sin_addr) <<
        "@" << return_info.port_num << std::endl;
#endif
#if (VERBOSE >= 2)
    std::cerr << my_time() << "     using FD " << return_info.socketFD <<
        std::endl;
#endif
    return return_info;
}
