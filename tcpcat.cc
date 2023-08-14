#include "commonutils.h"
#include "copyfd.h"
#include "mcleaner.h"
using namespace MCleaner;
#include "miscutils.h"
#include "netutils.h"

#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <thread>
#include <netdb.h>
#include <signal.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

// Tuning (compile time)
constexpr int listen_backlog{10};

void usage_error();  // Note: will be exported for use in commonutils.

static void responder(
    const ServerInfo server_info[2], Listener::SocketInfo final_info[2]);
static void handle_clients(const int sck[2]);
static void copy(int firstFD, int secondFD);

int main (int argc, char* argv[])
{
    // Process inputs
    int argc_copy = argc - 1;
    char** argv_copy = argv;
    ++argv_copy;
    Uri uri[2];
    uri[0]  = process_args(argc_copy, argv_copy);
    uri[1] = process_args(argc_copy, argv_copy);
    if (argc_copy != 0) usage_error();

    // For error reporting
    try
    {
    // Initialize for listening, and do a test on connecting.
    ServerInfo server_info[2];
    for (size_t index = 0 ; index < 2 ; ++index)
    {
        if (uri[index].listening)
        {
            server_info[index].listener = new Listener(
                uri[index].hostname, uri[index].ports, listen_backlog);
        }
        else
        {
            server_info[index].port_num = uri[index].ports[0];
        }
        server_info[index].hostname = std::move(uri[index].hostname);
        // Give user immediate feedback. Otherwise, error message would only
        // appear when connection is attempted. That could be much later.
        if (server_info[index].hostname != "")
        {
            struct hostent* server =
                gethostbyname(server_info[index].hostname.c_str());
            if (server == nullptr)
            {
                std::string str = "gethostbyname(";
                str += server_info[index].hostname;
                str += ") : ";
                str += strerror(h_errno);
                std::cerr << str << std::endl;
                exit(1);
            }
        }
    }

    // Programming note: user inputs processed, and uri is now obsolete.

    // Prevent a crash
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
    {
        perror("signal");
        exit(1);
    }
    if (signal(SIGTRAP, SIG_IGN) == SIG_ERR)
    {
        perror("signal");
        exit(1);
    }

    // Finish listening and connecting
    Listener::SocketInfo final_info[2];
    auto accept2 = [&server_info, &final_info] (int index) {
        final_info[index] =
            server_info[index].listener->get_client(0);
    };
    // Special processing for double listen
    if (server_info[0].listening() && server_info[1].listening())
    {
        // wait for both clients to accept
        std::thread t0(accept2, 0);
        std::thread t1(accept2, 1);
        t0.join();
        t1.join();
    }
    else
    {
        // Wait for client to listening socket, if any.
        for (size_t index = 0 ; index < 2 ; ++index)
        {
            if (server_info[index].listening())
            {
                accept2(index);
            }
            else
            {
                final_info[index].port_num =
                    server_info[index].port_num;
                final_info[index].socketFD = -1;
            }
        }
    }

    // Copy data
    responder(server_info, final_info);

    }
    catch (const NetutilsException& r)
    {
        std::cerr << my_time() << " " << r.strng << std::endl;
        exit(1);
    }

    return 0;
}

void usage_error()
{
    std::cerr << "Usage: tcpcat <input_spec> <output_spec>" << std::endl;
    std::cerr << "Each of <input_spec> and <output_spec> can be one of" <<
        std::endl;
    std::cerr << "    -pipe" << std::endl;
    std::cerr << "    -listen <port_number,port_number,...>" << std::endl;
    std::cerr << "    -listen <hostname>:<port_number,port_number,...>" << std::endl;
    std::cerr << "    -connect <hostname>:<port_number>" << std::endl;
    exit (1);
}

void responder(
    const ServerInfo server_info[2], Listener::SocketInfo final_info[2])
{
    int final_sock[2]{-1, -1};
    SocketCloser sc0(final_sock[0]);
    SocketCloser sc1(final_sock[1]);
    bool success = true;
    for (size_t index = 0 ; index < 2 ; ++index)
    {
        if (server_info[index].listening())
        {
            final_sock[index] = final_info[index].socketFD;
        }
        else
        {
            if (final_info[index].port_num != -1)
            {
                // Not stdin or stdout
                try
                {
                    final_sock[index] =
                        socket_from_address(
                            0, server_info[index].hostname,
                            final_info[index].port_num,
                            std::numeric_limits<unsigned>::max());
                }
                catch (const NetutilsException& r)
                {
                    std::cerr << my_time() << " " <<
                        r.strng << std::endl;
                    exit(1);
                }
                if (final_sock[index] == -1)
                {
                    if ((errno == ETIMEDOUT) ||
                        (errno == EINPROGRESS))
                    {
#if (VERBOSE >= 2)
                        std::cerr << my_time() <<
                            " Note: connect to listener: " <<
                            strerror(errno) << std::endl;
#endif
                        success = false;
                        break;
                    }
                    else
                    {
                        errorexit("connect to remote");
                    }
                }
            }
        }
    }
    if (success)
    {
        // handle_clients() will close both sockets at a
        // future time.
        sc0.disable();
        sc1.disable();
        handle_clients(final_sock);
    }
    else
    {
#if (VERBOSE >= 2)
        std::cerr << my_time() << " early closing " <<
            final_sock[0] << " " << final_sock[1] << std::endl;
#endif
    }
#if (VERBOSE >= 3)
    std::cerr << my_time() << " End copy loop FD " <<
        final_sock[0] << " <--> FD " << final_sock[1] << std::endl;
#endif
}

void handle_clients(const int sck[2])
{
#if (VERBOSE >= 3)
    std::cerr << my_time() << " Begin copy loop FD " << sck[0] << " <--> FD " <<
        sck[1] << std::endl;
#endif
    SocketCloser sc0(sck[0]);
    SocketCloser sc1(sck[1]);

    int sock[2];
    sock[0] = (sck[0] == -1) ? 0 : sck[0];
    sock[1] = (sck[1] == -1) ? 1 : sck[1];
    copy(sock[0], sock[1]);

#if (VERBOSE >= 3)
    std::cerr << my_time() << " closing FD " << sck[0] <<
        " FD " << sck[1] << std::endl;
#endif
}

void copy(int firstFD, int secondFD)
{
    set_flags(firstFD , O_NONBLOCK);
    set_flags(secondFD, O_NONBLOCK);
    try
    {
#if (VERBOSE >= 3)
        std::cerr << my_time() << " starting copy, FD " << firstFD <<
            " to FD " << secondFD << std::endl;
        auto stats = copyfd(firstFD, secondFD, 4*1024);
        std::cerr << my_time() << " FD " << firstFD << " --> FD " << secondFD <<
            ": " <<
            stats.bytes_copied << " bytes, " <<
            stats.reads << " reads, " <<
            stats.writes << " writes." << std::endl;
#else
        copyfd(firstFD, secondFD, 4*1024);
#endif
    }
    catch (const CopyFDReadException& r)
    {
        // I could not find documentation for this condition.
        // TODO: handle after calling  poll()?
        if (r.errn == ECONNREFUSED)
        {
            std::cerr << my_time() << " (reading) : "
                << strerror(ECONNREFUSED) << std::endl;
            exit(1);
        }
#if (VERBOSE >= 3)
        std::cerr << my_time() << " Read failure after " << r.byte_count <<
            " bytes: " << strerror(r.errn) << std::endl;
#endif
    }
    catch (const CopyFDWriteException& w)
    {
        if (w.errn == ECONNREFUSED)
        {
            std::cerr << my_time() << " (writing) : "
                << strerror(ECONNREFUSED) << std::endl;
            exit(1);
        }
#if (VERBOSE >= 3)
        std::cerr << my_time() << " Write failure after " << w.byte_count <<
            " bytes: " << strerror(w.errn) << std::endl;
#endif
    }
}
