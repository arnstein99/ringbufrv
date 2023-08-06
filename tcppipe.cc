#include <iostream>
#include <semaphore>
#include <limits>
#include <thread>
#include <chrono>
using namespace std::chrono_literals;
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "copyfd.h"
#include "miscutils.h"
#include "netutils.h"
#include "mcleaner.h"
using namespace MCleaner;

// Tuning (compile time)
constexpr std::ptrdiff_t default_max_cip{10};
constexpr std::ptrdiff_t default_max_clients{32};
constexpr unsigned default_max_connecttime_s{300};
constexpr std::ptrdiff_t semaphore_max_max{256};
constexpr int listen_backlog{10};

struct Options
{
    std::ptrdiff_t max_cip;
    std::ptrdiff_t max_clients;
    unsigned max_iotime_s;
    unsigned max_connecttime_s;
};
struct Uri
{
    bool listening;
    std::vector<int> ports;  // -1 means stdin or stdout
    std::string hostname;    // Not always defined
};

struct ServerInfo
{
    ServerInfo() : port_num(-1), listener(nullptr) { }
    ~ServerInfo() { if (listener) delete listener; }
    inline bool listening() const { return (listener != nullptr); }
    std::string hostname;    // Not always defined
    int port_num;            // Not defined if listening
    Listener* listener;
};

static Options process_options(int& argc, char**& argv);
static Uri process_args(int& argc, char**& argv);

static void handle_clients(int sck[2], const Options& opt);
static void copy(int firstFD, int secondFD, const std::atomic<bool>& cflag);
static void usage_error();

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
    // Loop over clients
    do
    {
        clients_limiter.acquire();
        Listener::SocketInfo final_info[2];
        auto accept2 = [&server_info, &final_info] (int index) {
            final_info[index] =
                server_info[index].listener->get_client();
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

        ByValue<Listener::SocketInfo,2> fi(final_info);
        auto responder =
            [&clients_limiter, &cip_limiter, &server_info, fi, &options] ()
            {
                SemaphoreReleaser clientsToken(clients_limiter);
                int final_sock[2]{-1, -1};
                cip_limiter.acquire();
                bool success = true;
                {   SemaphoreReleaser cip_token(cip_limiter);
                    for (size_t index = 0 ; index < 2 ; ++index)
                    {
                        if (!server_info[index].listening())
                        {
                            if (fi[index].port_num > 1)
                            {
                                try
                                {
                                    final_sock[index] =
                                        socket_from_address(
                                            server_info[index].hostname,
                                            fi[index].port_num,
                                            options.max_connecttime_s);
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
                                        std::cerr << my_time() <<
                                            " Note: connect to listener: " <<
                                            strerror(errno) << std::endl;
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
                        else
                        {
                            final_sock[index] = fi[index].socketFD;
                        }
                    }
                }
                if (success)
                {
                    handle_clients(final_sock, options);
                    // handle_clients() will close both sockets at a
                    // future time.
                }
                else
                {
                    if (final_sock[0] > 2) close(final_sock[0]);
                    if (final_sock[1] > 2) close(final_sock[1]);
#if (VERBOSE >= 2)
                    std::cerr << my_time() << " early closing " <<
                        final_sock[0] << " " << final_sock[1] << std::endl;
#endif
                }
            };
        last_thread = std::thread(responder);
        if (repeat) last_thread.detach();
#if (VERBOSE >= 3)
        std::cerr << my_time() << " End copy loop" << std::endl;
#endif
    } while (repeat);
    last_thread.join();

    }
    catch (const NetutilsException& r)
    {
        std::cerr << my_time() << " " << r.strng << std::endl;
        exit(1);
    }

#if (VERBOSE >= 3)
    std::cerr << my_time() << " Normal exit" << std::endl;
#endif
    return 0;
}

static Options process_options(int& argc, char**& argv)
{
    if (argc < 2) usage_error();
    Options options;
    options.max_cip = default_max_cip;
    options.max_clients = default_max_clients;
    options.max_iotime_s = std::numeric_limits<unsigned>::max();
    options.max_connecttime_s = default_max_connecttime_s;

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
            options.max_iotime_s = mstoi(value, true);
            options.max_connecttime_s = options.max_iotime_s;
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

static Uri process_args(int& argc, char**& argv)
// Group can be one of
//     -stdio
//     -listen <port,port,...>
//     -listen <address>:<port,port,...>
//     -connect <hostname> <port>
{
    Uri uri;

    if (argc < 1) usage_error();
    const char* option = argv[0];
    ++argv;
    --argc;

    if (strcmp(option, "-stdio") == 0)
    {
        uri.ports.push_back(-1);
        uri.listening = false;
    }
    else if (strcmp(option, "-listen") == 0)
    {
        uri.listening = true;
        if (argc < 1) usage_error();
        const char* value = argv[0];
        ++argv;
        --argc;
        auto vec = mstrtok(value, ':');
        std::vector<std::string> vec2;
        switch (vec.size())
        {
        // TODO: eliminate redundancy in the next two cases
        case 1:
            uri.hostname = "";
            vec2 = mstrtok(vec[0], ',');
            if (vec2.size() == 0) usage_error();
            for (auto value2 : vec2)
            {
                uri.ports.push_back(mstoi(value2));
            }
            break;
        case 2:
            uri.hostname = vec[0];
            vec2 = mstrtok(vec[1], ',');
            if (vec2.size() == 0) usage_error();
            for (auto value2 : vec2)
            {
                uri.ports.push_back(mstoi(value2));
            }
            break;
        default:
            usage_error();
            break;
        }
    }
    else if (strcmp(option, "-connect") == 0)
    {
        uri.listening = false;
        if (argc < 1) usage_error();
        const char* value = argv[0];
        uri.hostname = value;
        --argc;
        ++argv;
        auto vec = mstrtok(value, ':');
        switch (vec.size())
        {
        case 2:
            uri.hostname = vec[0];
            uri.ports.push_back(mstoi(vec[1]));
            break;
        default:
            usage_error();
            break;
        }
    }
    else
    {
        usage_error();
    }
    return uri;
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

void handle_clients(int sck[2], const Options& opt)
{
#if (VERBOSE >= 3)
    std::cerr << my_time() << " Begin copy loop FD " << sck[0] << " <--> FD " <<
        sck[1] << std::endl;
#endif
    std::atomic<bool> continue_flag = true;
    std::counting_semaphore<2> copy_semaphore(2);

    copy_semaphore.acquire();
    auto proc1 = [sck, &continue_flag, &copy_semaphore] ()
    {
        SemaphoreReleaser token(copy_semaphore);
        int sock[2];
        sock[0] = (sck[0] == -1) ? 0 : sck[0];
        sock[1] = (sck[1] == -1) ? 1 : sck[1];
        copy(sock[0], sock[1], continue_flag);
        continue_flag = false;
    };
    std::thread one(proc1);

    copy_semaphore.acquire();
    auto proc2 = [sck, &continue_flag, &copy_semaphore] ()
    {
        SemaphoreReleaser token(copy_semaphore);
        int sock[2];
        sock[1] = (sck[1] == -1) ? 0 : sck[1];
        sock[0] = (sck[0] == -1) ? 1 : sck[0];
        copy(sock[1], sock[0], continue_flag);
        continue_flag = false;
    };
    std::thread two(proc2);

#if (VERBOSE >= 3)
    int normal = copy_semaphore.try_acquire_for(opt.max_iotime_s * 1s);
    if (!normal)
    {
        std::cerr << my_time() << " Note: client terminated" << std::endl;
    }
#else
    copy_semaphore.try_acquire_for(opt.max_iotime_s * 1s);
#endif
    continue_flag = false;
    one.join();
    two.join();
#if (VERBOSE >= 3)
    std::cerr << my_time() << " closing FD " << sck[0] <<
        " FD " << sck[1] << std::endl;
#endif
    if (sck[0] > 2) close (sck[0]);
    if (sck[1] > 2) close (sck[1]);
}

static void copy(int firstFD, int secondFD, const std::atomic<bool>& cflag)
{
    set_flags(firstFD , O_NONBLOCK);
    set_flags(secondFD, O_NONBLOCK);
    try
    {
#if (VERBOSE >= 3)
        std::cerr << my_time() << " starting copy, FD " << firstFD <<
            " to FD " << secondFD << std::endl;
        auto stats = copyfd_while(firstFD, secondFD, cflag, 500000, 4*1024);
        std::cerr << my_time() << " FD " << firstFD << " --> FD " << secondFD <<
            ": " <<
            stats.bytes_copied << " bytes, " <<
            stats.reads << " reads, " <<
            stats.writes << " writes." << std::endl;
#else
        copyfd_while(firstFD, secondFD, cflag, 500000, 4*1024);
#endif
    }
    catch (const CopyFDReadException& r)
    {
#if (VERBOSE >= 3)
        std::cerr << my_time() << " Read failure after " << r.byte_count <<
            " bytes: " << strerror(r.errn) << std::endl;
#endif
    }
    catch (const CopyFDWriteException& w)
    {
#if (VERBOSE >= 3)
        std::cerr << my_time() << " Write failure after " << w.byte_count <<
            " bytes: " << strerror(w.errn) << std::endl;
#endif
    }
}
