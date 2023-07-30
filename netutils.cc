#include "netutils.h"
#include "miscutils.h"

#include <thread>
#include <chrono>
using namespace std::chrono_literals;
#include <string.h>
#include <fcntl.h>
#include <netdb.h>
#include <arpa/inet.h>

#if (VERBOSE >= 1)
#include <iostream>
#endif

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
    const std::string& hostname, int port_number, bool do_connect,
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

    if (do_connect == false)
    {
        if (hostname == "")
        {
            serveraddr.sin_addr.s_addr = htonl (INADDR_ANY);
        }
        else
        {
            const char* host_string = hostname.c_str();
            serveraddr.sin_addr.s_addr = inet_addr(host_string);
        }
        set_reuse(socketFD);

        // Bind to address but do not connect to anything
        NEGCHECK("bind",
            bind(
                socketFD,
                (struct sockaddr *)(&serveraddr),
                (socklen_t)sizeof (serveraddr)));
#if (VERBOSE >= 2)
            std::cerr << my_time() << " bound socket " << socketFD << std::endl;
#endif
    }
    else
    {
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
    std::cerr << my_time() << " connected " <<
        inet_ntoa(serveraddr.sin_addr) << std::endl;
#endif
#if (VERBOSE >= 2)
            std::cerr << my_time() << " connected socket " << socketFD <<
                std::endl;
#endif
    }

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
        std::endl;
#endif
#if (VERBOSE >= 2)
    std::cerr << my_time() << " accepted on socket " << listening_socket <<
        std::endl;
    std::cerr << my_time() << " accepted socket " << client_socket <<
        " on listening socket " << listening_socket << std::endl;
#endif

    return client_socket;
}

void get_two_clients(const int listening_socket[2], int client_socket[2])
{
    int maxfd =
        std::max(listening_socket[0], listening_socket[1]) + 1;
    fd_set read_set;
    int cl_socket[2] = {-1, -1};
    do
    {
        fd_set* p_read_set = nullptr;
        FD_ZERO(&read_set);

        auto no_wait_listen = [&] (int index)
        {
            if (cl_socket[index] < 0)
            {
                struct sockaddr_in addr;
                socklen_t addrlen = (socklen_t)sizeof(addr);
                cl_socket[index] =
                    accept(
                        listening_socket[index],
                        (struct sockaddr*)(&addr), &addrlen);
                if (cl_socket[index] < 0)
                {
                    if ((errno == EWOULDBLOCK) || (errno == EAGAIN))
                    {
                        // This is when to select()
                        FD_SET(listening_socket[index], &read_set);
                        p_read_set = &read_set;
                    }
                    else
                    {
                        // Some other error on input
                        errorexit("accept");
                    }
                }
                else
                {
#if (VERBOSE >= 1)
                    std::cerr << my_time() << " accepted " <<
                        inet_ntoa(addr.sin_addr) << std::endl;
#endif
#if (VERBOSE >= 2)
                    std::cerr << my_time() << "accepted socket " <<
                        cl_socket[index] << " on listening socket " <<
                        listening_socket[index] << std::endl;
#endif
                }
            }
        };
        no_wait_listen(0);
        no_wait_listen(1);

        if (p_read_set)
        {
            NEGCHECK("select",
                (select(maxfd, p_read_set, nullptr, nullptr, nullptr)));
        }

    } while ((cl_socket[0] < 0) || (cl_socket[1] < 0));
#if (VERBOSE >= 2)
    std::cerr << my_time() << " finished accepts" << std::endl;
#endif
    client_socket[0] = cl_socket[0];
    client_socket[1] = cl_socket[1];
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
                std::cerr << "select returns " << retval <<
                    ", 1 was expected." << std::endl;
                retval = -1;
                break;
            }
        }
    }
    return retval;
}
