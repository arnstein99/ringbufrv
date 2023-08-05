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
    if (server == nullptr) errorexit("gethostbyname");
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
    std::cerr << my_time() << " using port " << port_number << "," <<
        std::endl;
    std::cerr << my_time() << " connected " <<
        inet_ntoa(serveraddr.sin_addr) << ":" << socketFD << std::endl;
#endif
#if (VERBOSE >= 2)
        std::cerr << my_time() << " connected socket " << socketFD <<
            std::endl;
#endif

    return socketFD;
}

int get_client(int listening_socket)
{
    struct sockaddr_in addr;
    socklen_t addrlen = (socklen_t)sizeof(addr);
    int client_socket;
    NEGCHECK("accept", (client_socket = accept(
        listening_socket, (struct sockaddr*)(&addr), &addrlen)));
#if (VERBOSE >= 1)
    std::cerr << my_time() << " accepted " << inet_ntoa(addr.sin_addr) <<
        ":" << client_socket << std::endl;
#endif
#if (VERBOSE >= 2)
    std::cerr << my_time() << " accepted on socket " << listening_socket <<
        std::endl;
    std::cerr << my_time() << " accepted socket " << client_socket <<
        " on listening socket " << listening_socket << std::endl;
#endif

    return client_socket;
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
#if (VERBOSE >= 1)
                std::cerr << "select returns " << retval <<
                    ", 1 was expected." << std::endl;
#endif
                retval = -1;
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
            std::cerr << "Cannot use \"" << hostname <<
                "\" with -listen." << std::endl;
            std::cerr << "Please use numbers, e.g. 12.34.56.78 ." <<
                std::endl;
            exit(1);
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
#if (VERBOSE >= 1)
    std::cerr << my_time() << " using port " << listening_info.port_num <<
        "," << std::endl;
#endif
    return_info.socketFD = ::get_client(listening_info.socketFD);

    return return_info;
}
