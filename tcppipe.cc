#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <thread>
#include <iostream>
#include <semaphore>
#include <chrono>
#include <limits>
using namespace std::chrono_literals;
#include <cstring>
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
    int port;                // -1 means stdin or stdout
    std::string hostname;    // Not always defined
};

static Options process_options(int& argc, char**& argv);
static Uri process_args(int& argc, char**& argv);

// For passing by value instead of by reference
struct Int2
{
public:
    Int2() :  val{0,0} {}
    Int2(const int other[2]) { val[0]=other[0]; val[1]=other[1]; }
    Int2(const Int2& int2) { val[0]=int2.val[0]; val[1]=int2.val[1]; }
    Int2& operator=(const int other[2]) {
        val[0]=other[0]; val[1]=other[1]; return *this; }
    Int2& operator=(const Int2& int2) {
        if (this == &int2) return *this;
        val[0]=int2.val[0];
        val[1]=int2.val[1];
        return *this;
    }

          int& operator[] (size_t index)       { return val[index]; }
    const int& operator[] (size_t index) const { return val[index]; }

    // A bit of a hack
    int* reference() { return val; }

private:
    int val[2];
};

static void handle_clients(Int2 sck, const Uri* ur, const Options* opt);
static void copy(int firstFD, int secondFD, const std::atomic<bool>& cflag);
static void usage_error();

int main(int argc, char* argv[])
{
    // Process user inputs

    int argc_copy = argc - 1;
    char** argv_copy = argv + 1;
    Options options = process_options(argc_copy, argv_copy);
    Uri uri[2];
    uri[0] = process_args(argc_copy, argv_copy);
    uri[1] = process_args(argc_copy, argv_copy);
    if (argc_copy != 0) usage_error();

    int listener[2] = {-1, -1};
    // Special processing for double listen
    bool no_block = (uri[0].listening && uri[1].listening);
    auto prepar_if = [&no_block, &listener, &uri] (int index)
    {
        if (uri[index].listening)
        {
            listener[index] =
                socket_from_address(
                    uri[index].hostname, uri[index].port, false);
            if (no_block) set_flags(listener[index], O_NONBLOCK);
            NEGCHECK("listen", listen(listener[index], listen_backlog));
        }
    };
    prepar_if(0);
    prepar_if(1);

    // Prevent a crash
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
    {
        perror("signal");
        exit(1);
    }

    bool repeat = (uri[0].listening || uri[1].listening);
    std::thread last_thread;
    std::counting_semaphore<semaphore_max_max>
        clients_limiter{options.max_clients};
    std::counting_semaphore<semaphore_max_max> cip_limiter{options.max_cip};
    // Loop over clients
    do
    {
        clients_limiter.acquire();
#if (VERBOSE >= 2)
        std::cerr << my_time() << " Begin copy loop" << std::endl;
#endif
        int accepted_sock[2];
        // Special processing for double listen
        if (uri[0].listening && uri[1].listening)
        {
            // wait for both clients to accept
            get_two_clients(listener, accepted_sock);
        }
        else
        {
            // Wait for client to listening socket, if any.
            for (size_t index = 0 ; index < 2 ; ++index)
            {
                if (uri[index].listening)
                {
                    accepted_sock[index] = get_client(listener[index]);
                }
            }
        }

        auto responder =
            [&clients_limiter, &cip_limiter, accepted_sock, uri, options]()
            {
                SemaphoreReleaser clientsToken(clients_limiter);
                int final_sock[2]{-1, -1};
                FileCloser closer0(final_sock[0]);
                FileCloser closer1(final_sock[1]);
                cip_limiter.acquire();
                bool success = true;
                {   SemaphoreReleaser cip_token(cip_limiter);
                    for (size_t index = 0 ; index < 2 ; ++index)
                    {
                        if (uri[index].listening)
                        {
                            final_sock[index] = accepted_sock[index];
                        }
                        else
                        {
                            if (uri[index].port > 1)
                            {
                                final_sock[index] =
                                    socket_from_address(
                                        uri[index].hostname, uri[index].port,
                                        true, options.max_connecttime_s);
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
                    }
                }
                if (success)
                {
                    handle_clients(final_sock, uri, &options);
                }
#if (VERBOSE >= 1)
                std::cerr << my_time() << " closing " << final_sock[0] <<
                    " " << final_sock[1] << std::endl;
#endif
            };
        last_thread = std::thread(responder);
        if (repeat) last_thread.detach();
#if (VERBOSE >= 2)
        std::cerr << my_time() << " End copy loop" << std::endl;
#endif
    } while (repeat);
    last_thread.join();

#if (VERBOSE >= 1)
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
//     -listen <port>
//     -listen <address>:<port>
//     -connect <hostname> <port>
{
    Uri uri;

    if (argc < 1) usage_error();
    const char* option = argv[0];
    ++argv;
    --argc;

    if (strcmp(option, "-stdio") == 0)
    {
        uri.listening = false;
        uri.port = -1;
    }
    else if (strcmp(option, "-listen") == 0)
    {
        uri.listening = true;
        if (argc < 1) usage_error();
        const char* value = argv[0];
        ++argv;
        --argc;
        auto vec = mstrtok(value, ':');
        switch (vec.size())
        {
        case 1:
            uri.hostname = "";
            uri.port = mstoi(vec[0]);
            break;
        case 2:
            uri.hostname = vec[0];
            uri.port = mstoi(vec[1]);
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
            uri.port = mstoi(vec[1]);
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
    std::cerr << "    -listen <port_number>" << std::endl;
    std::cerr << "    -listen <address>:<port_number>" << std::endl;
    std::cerr << "    -connect <hostname>:<port_number>" << std::endl;
    exit (1);
}

void handle_clients(Int2 sck, const Uri* ur, const Options* opt)
{
#if (VERBOSE >= 2)
    std::cerr << my_time() << " Begin copy loop " << sck[0] << " <--> " <<
        sck[1] << std::endl;
#endif
    std::atomic<bool> continue_flag = true;
    std::counting_semaphore<2> copy_semaphore(2);

    copy_semaphore.acquire();
    auto proc1 = [&ur, &sck, &continue_flag, &copy_semaphore] ()
    {
        SemaphoreReleaser token(copy_semaphore);
        if (ur[0].port == -1) sck[0] = 0;
        if (ur[1].port == -1) sck[1] = 1;
        copy(sck[0], sck[1], continue_flag);
        continue_flag = false;
    };
    std::thread one(proc1);

    copy_semaphore.acquire();
    auto proc2 = [&ur, &sck, &continue_flag, &copy_semaphore] ()
    {
        SemaphoreReleaser token(copy_semaphore);
        if (ur[0].port == -1) sck[0] = 1;
        if (ur[1].port == -1) sck[1] = 0;
        copy(sck[1], sck[0], continue_flag);
        continue_flag = false;
    };
    std::thread two(proc2);

#if (VERBOSE >= 1)
    int normal = copy_semaphore.try_acquire_for(opt->max_iotime_s * 1s);
    if (!normal)
    {
        std::cerr << my_time() << " Note: client terminated" << std::endl;
    }
#else
    copy_semaphore.try_acquire_for(opt->max_iotime_s * 1s);
#endif
    continue_flag = false;
    one.join();
    two.join();
}

static void copy(int firstFD, int secondFD, const std::atomic<bool>& cflag)
{
    set_flags(firstFD , O_NONBLOCK);
    set_flags(secondFD, O_NONBLOCK);
    try
    {
#if (VERBOSE >= 2)
        std::cerr << my_time() << " starting copy, FD " << firstFD <<
            " to FD " << secondFD << std::endl;
        auto stats = copyfd_while(firstFD, secondFD, cflag, 500000, 4*1024);
        std::cerr << stats.bytes_copied << " bytes, " <<
            stats.reads << " reads, " <<
            stats.writes << " writes." << std::endl;
#else
        copyfd_while(firstFD, secondFD, cflag, 500000, 4*1024);
#endif
    }
    catch (const ReadException& r)
    {
#if (VERBOSE >= 1)
        std::cerr << my_time() << " Read failure after " << r.byte_count <<
            " bytes: " << strerror(r.errn) << std::endl;
#endif
    }
    catch (const WriteException& w)
    {
#if (VERBOSE >= 1)
        std::cerr << my_time() << " Write failure after " << w.byte_count <<
            " bytes: " << strerror(w.errn) << std::endl;
#endif
    }
}
