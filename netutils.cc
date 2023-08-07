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
            fd_set set;
            int maxfd = sockfd + 1;
            FD_ZERO(&set);
            FD_SET(sockfd, &set);
            struct timeval tv;
            tv.tv_sec = maxwait_s;
            tv.tv_usec = 0;
            retval = select(maxfd, nullptr, &set, nullptr, &tv);
            switch (retval)
            {
            case 0:
                // Timeout
                retval = -1;
                break;
            case 1:
                // Success
                retval = 0;
                break;
            default:
                std::string str = "select() : ";
                str += strerror(errno);
                NetutilsException r(str);
                throw(r);
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
    FD_ZERO(&master_set);
    maxfd = 0;
    for (auto port_num : ports)
    {
        int socketFD;
        NEGCHECK(
            "socket", socketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP));
        set_reuse(socketFD);
        set_flags(socketFD, O_NONBLOCK);
        int optval = 1;
        NEGCHECK("setsockopt",
            setsockopt(socketFD, SOL_SOCKET, SO_KEEPALIVE, &optval,
            sizeof(optval)));

        sa.sin_port = htons ((uint16_t)port_num);
        NEGCHECK("bind",
            bind(socketFD, (struct sockaddr *)(&sa), (socklen_t)sizeof (sa)));
        NEGCHECK("listen",  listen(socketFD, backlog));
        FD_SET (socketFD, &master_set);
        maxfd = std::max(maxfd, socketFD);
        SocketInfo info;
        info.socketFD = socketFD;
        info.port_num = port_num;
        socket_infos.push_back(info);
    }
}

Listener::SocketInfo Listener::get_client()
{
    fd_set read_set = master_set;
    NEGCHECK("select", select(maxfd+1, &read_set, nullptr, nullptr, nullptr));
    SocketInfo listening_info;
    listening_info.socketFD = -1;
    do
    {
        for (auto info : socket_infos)
        {
            if (FD_ISSET(info.socketFD, &read_set))
            {
                listening_info = info;
                break;
            }
        }
    } while (listening_info.socketFD == -1); // Handles spurious connections

    SocketInfo return_info;
    return_info.port_num = listening_info.port_num;

    struct sockaddr_in addr;
    socklen_t addrlen = (socklen_t)sizeof(addr);
    NEGCHECK("accept", (return_info.socketFD = accept(
        listening_info.socketFD, (struct sockaddr*)(&addr), &addrlen)));

#if (VERBOSE >= 1)
    std::cerr << my_time() << " accepted " << inet_ntoa(addr.sin_addr) <<
        "@" << listening_info.port_num << std::endl;
#endif
#if (VERBOSE >= 2)
    std::cerr << my_time() << "     using FD " << return_info.socketFD <<
        std::endl;
#endif
    return return_info;
}
