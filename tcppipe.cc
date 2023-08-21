#include "commonutils.h"
#include "copyfd.h"
#include "mcleaner.h"
#include "miscutils.h"
#include "netutils.h"
using namespace MCleaner;

#include <chrono>
using namespace std::chrono_literals;
#include <cstring>
#include <iostream>
#include <semaphore>
#include <thread>

#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

// Tuning (compile time)
#ifndef BUFFER_SIZE
#define BUFFER_SIZE (4*1024)
#endif
constexpr std::ptrdiff_t default_max_cip{10};
constexpr std::ptrdiff_t default_max_clients{32};
constexpr int default_max_connecttime_ms{300*1000};
constexpr std::ptrdiff_t semaphore_max_max{256};
constexpr int listen_backlog{10};

struct Options
{
    std::ptrdiff_t max_cip;
    std::ptrdiff_t max_clients;
    int max_iotime_ms;
    int max_connecttime_ms;
};
static Options process_options(int& argc, char**& argv);
static void handle_clients(
    unsigned client_num, const int sck[2], int max_iotime_ms);
void usage_error();  // Note: will be exported for use in commonutils.

int main(int argc, char* argv[])
{
    // For error reporting
    try
    {
    // Process user inputs

    int argc_copy = argc - 1;
    char** argv_copy = argv + 1;
    Options options = process_options(argc_copy, argv_copy);
    Uri uri[2];
    uri[0] = process_args(argc_copy, argv_copy);
    uri[1] = process_args(argc_copy, argv_copy);
    if (argc_copy != 0) usage_error();

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
            // Programming note: a value of -1 indicates stdin or stdout.
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

    bool repeat =
        (server_info[0].listening() || server_info[1].listening());
    std::thread last_thread;
    std::counting_semaphore<semaphore_max_max>
        clients_limiter{options.max_clients};
    std::counting_semaphore<semaphore_max_max> cip_limiter{options.max_cip};
    unsigned client_num{0};
    // Loop over clients
    do
    {
        clients_limiter.acquire();
        ++client_num;
        Listener::SocketInfo final_info[2];
        auto accept2 = [client_num, &server_info, &final_info] (int index) {
            final_info[index] =
                server_info[index].listener->get_client(client_num);
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
                    final_info[index].port_num = server_info[index].port_num;
                }
            }
        }

        ByValue<Listener::SocketInfo,2> fi(final_info);
        auto responder =
            [client_num, &clients_limiter, &cip_limiter, &server_info, fi,
                &options] ()
            {
                SemaphoreReleaser clientsToken(clients_limiter);
                int final_sock[2]{-1, -1};
                SocketCloser sc0(final_sock[0]);
                SocketCloser sc1(final_sock[1]);
                cip_limiter.acquire();
                bool success = true;
                {   SemaphoreReleaser cip_token(cip_limiter);
                    for (size_t index = 0 ; index < 2 ; ++index)
                    {
                        if (server_info[index].listening())
                        {
                            final_sock[index] = fi[index].socketFD;
                        }
                        else
                        {
                            if (fi[index].port_num != -1)
                            {
                                // Not stdin or stdout
                                try
                                {
                                    final_sock[index] =
                                        socket_from_address(
                                            client_num,
                                            server_info[index].hostname,
                                            fi[index].port_num,
                                            options.max_connecttime_ms);
                                    if (final_sock[index] == -1)
                                    {
                                        if ((errno == ETIMEDOUT) ||
                                            (errno == EINPROGRESS))
                                        {
#if (VERBOSE >= 3)
                                            std::cerr <<
                                                my_prefix(client_num) <<
                                                "Note: connect to listener: " <<
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
                                catch (const NetutilsException& r)
                                {
                                    std::cerr << my_prefix(client_num) <<
                                        r.strng << std::endl;
                                    exit(1);
                                }
                            }
                        }
                    }
                }
                if (success)
                {
                    handle_clients(
                        client_num, final_sock, options.max_iotime_ms);
                }
                else
                {
#if (VERBOSE >= 3)
                    std::cerr << my_prefix(client_num) <<
                        "early closing " << final_sock[0] << " " <<
                        final_sock[1] << std::endl;
#endif
                }
#if (VERBOSE >= 2)
                std::cerr << my_prefix(client_num) <<
                    "End copy loop FD " << final_sock[0] << " <--> FD " <<
                    final_sock[1] << std::endl;
#endif
            };
        last_thread = std::thread(responder);
        if (repeat) last_thread.detach();
    } while (repeat);
    last_thread.join();

    }
    catch (const NetutilsException& r)
    {
        std::cerr << my_time() << " " << r.strng << std::endl;
        exit(1);
    }

#if (VERBOSE >= 2)
    std::cerr << my_time() << " Normal exit" << std::endl;
#endif
    return 0;
}

Options process_options(int& argc, char**& argv)
{
    if (argc < 2) usage_error();
    Options options;
    options.max_cip = default_max_cip;
    options.max_clients = default_max_clients;
    options.max_iotime_ms = -1;
    options.max_connecttime_ms = default_max_connecttime_ms;

    while (argc > 2)
    {
        const char* option = argv[0];
        if (strcmp(option, "-max_cip") == 0)
        {
            if (argc < 1) usage_error();
            const char* value = argv[1];
            options.max_cip = mstoi(value);
            argv += 2;
            argc -=2;
            if (options.max_cip > semaphore_max_max)
            {
                 std::cerr << "Sorry, \"-max_cip\" cannot be greater than " <<
                     semaphore_max_max << "." << std::endl;
                std::cerr <<
                    "Recompile program with a larger \"semaphore_max_max\" "
                        "to correct." << std::endl;
                exit(1);
            }
        }
        else if (strcmp(option, "-max_clients") == 0)
        {
            if (argc < 1) usage_error();
            const char* value = argv[1];
            options.max_clients = mstoi(value);
            argv += 2;
            argc -=2;
            if (options.max_clients > semaphore_max_max)
            {
                 std::cerr <<
                     "Sorry, \"-options.max_clients\" cannot be greater than " <<
                     semaphore_max_max << "." << std::endl;
                std::cerr <<
                    "Recompile program with a larger \"semaphore_max_max\" "
                        "to correct." << std::endl;
                exit(1);
            }
        }
        else if (strcmp(option, "-max_iotime") == 0)
        {
            if (argc < 1) usage_error();
            const char* value = argv[1];
            auto msec = 1000 * mstoi(value, true);
            options.max_iotime_ms = msec;
            options.max_connecttime_ms = msec;
            argv += 2;
            argc -=2;
        }
        else
        {
            break;
        }
    }
    return options;
}

void usage_error()
{
    std::cerr << "Usage:" << std::endl;
    std::cerr << "  tcppipe [-max_clients nnn(" << default_max_clients <<
        ")] [-max_cip nnn(" << default_max_cip <<
        ")] [-max_iotime nnn(lots)] " <<
        std::endl;
    std::cerr << "    <first_spec> <second_spec>" << std::endl;
    std::cerr << "Each of <first_spec> and <second_spec> can be one of" <<
        std::endl;
    std::cerr << "    -stdio" << std::endl;
    std::cerr << "    -listen <port_number,port_number,...>" << std::endl;
    std::cerr << "    -listen <address>:<port_number,port_number,...>" <<
        std::endl;
    std::cerr << "    -connect <hostname>:<port_number>" << std::endl;
    exit (1);
}

void handle_clients(
    unsigned client_num, const int sck[2], int max_iotime_ms)
{
#if (VERBOSE >= 1)
    my_prefix mp(client_num);
#endif
#if (VERBOSE >= 2)
    std::cerr << mp << "Begin copy loop FD " << sck[0] <<
        " <--> FD " << sck[1] << std::endl;
#endif

    try
    {
#if (VERBOSE >= 3)
        iopackage_stats stats[2];
#else
        iopackage_stats* stats(nullptr);
#endif
        copyfd2<BUFFER_SIZE>(sck[0], sck[1], max_iotime_ms, stats);
#if (VERBOSE >= 3)
        std::cerr << mp << "FD " << sck[0] << " --> FD " << sck[1] <<
            ": " <<
            stats[0].bytes_copied << " bytes, " <<
            stats[0].reads << " reads, " <<
            stats[0].writes << " writes." << std::endl;
        std::cerr << mp << "FD " << sck[1] << " --> FD " << sck[0] <<
            ": " <<
            stats[1].bytes_copied << " bytes, " <<
            stats[1].reads << " reads, " <<
            stats[1].writes << " writes." << std::endl;
#endif
    }
    catch (const IOPackageReadException& r)
    {
        // I could not find documentation for this condition.
        // TODO: handle after calling  poll()?
        if (r.errn == ECONNREFUSED)
        {
            std::cerr << my_prefix(client_num) << "(reading) : " <<
                strerror(ECONNREFUSED) << std::endl;
            exit(1);
        }
#if (VERBOSE >= 3)
        std::cerr << mp << "Read failure after " << r.byte_count <<
            " bytes: " << strerror(r.errn) << std::endl;
#endif
    }
    catch (const IOPackageWriteException& w)
    {
        if (w.errn == ECONNREFUSED)
        {
            std::cerr << my_prefix(client_num) << "(writing) : " <<
                strerror(ECONNREFUSED) << std::endl;
            exit(1);
        }
#if (VERBOSE >= 3)
        std::cerr << mp << "Write failure after " << w.byte_count <<
            " bytes: " << strerror(w.errn) << std::endl;
#endif
    }
#if (VERBOSE >= 3)
    std::cerr << mp << "closing FD " << sck[0] << " FD " << sck[1] << std::endl;
#endif
}

#include "copyfd.tcc"
