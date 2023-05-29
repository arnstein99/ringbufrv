#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <thread>
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "copyfd.h"
#include "miscutils.h"
#include "netutils.h"

struct Uri
{
    bool listening;
    int port;                // -1 means stdin or stdout
    std::string hostname;    // Not always defined
};
static Uri process_args(int& argc, char**& argv);

static void copy(int firstFD, int secondFD, const std::atomic<bool>& cflag);
static void usage_error();

int main (int argc, char* argv[])
{
    // Process user inputs
    int argc_copy = argc - 1;
    char** argv_copy = argv;
    ++argv_copy;
    Uri uri[2];
    uri[0] = process_args(argc_copy, argv_copy);
    uri[1] = process_args(argc_copy, argv_copy);
    if (argc_copy != 0) usage_error();

    // Get the listening sockets ready (if any)
    int listener[2] = {-1, -1};
    static auto socket_if = [&listener, &uri] (int index)
    {
        if (uri[index].listening)
        {
            listener[index] =
                socket_from_address(
                    uri[index].hostname, uri[index].port, false);
        }
    };
    socket_if(0);
    socket_if(1);

    // Prevent a crash
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
    {
        perror("signal");
        exit(1);
    }

    int sock[2] = {0, 0};
    std::atomic<bool> continue_flag;
    bool repeat = uri[0].listening || uri[1].listening;

    // Loop over copy tasks
    do
    {
        // Special processing for double listen
        if (uri[0].listening && uri[1].listening)
        {
            // wait for both URIs to accept
            get_two_clients(listener, sock);
        }
        else
        {
            // Wait for client to listening socket, if any.
            static auto listen_if = [&uri, &sock, &listener] (int index)
            {
                if (uri[index].listening)
                {
                    sock[index] = get_client(listener[index]);
                }
            };
            listen_if(0);
            listen_if(1);

            // Connect client socket and/or stdio socket, if any.
            static auto connect_if = [&uri, &sock] (int index)
            {
                if (!uri[index].listening)
                {
                    if (uri[index].port == -1)
                    {
                        // Will replace this with 0 or 1 eventually
                        sock[index] = -1;
                    }
                    else
                    {
                        sock[index] =
                            socket_from_address(
                                uri[index].hostname, uri[index].port, true);
                    }
                }
            };
            connect_if(0);
            connect_if(1);
        }

        // Both sockets are complete, so copy now.
#if (VERBOSE >= 2)
        std::cerr << my_time() << " Begin copy loop" << std::endl;
#endif
        continue_flag = true;
        static auto proc1 = [&sock, &continue_flag] ()
        {
            copy(sock[0], sock[1], continue_flag);
            continue_flag = false;
        };
        std::thread one(proc1);
        static auto proc2 = [&sock, &continue_flag] ()
        {
            copy(sock[1], sock[0], continue_flag);
            continue_flag = false;
        };
        std::thread two(proc2);

        two.join();
        one.join();
        if (uri[1].port != -1) close(sock[1]);
        if (uri[0].port != -1) close(sock[0]);
#if (VERBOSE >= 2)
        std::cerr << my_time() << " End copy loop" << std::endl;
#endif

    } while (repeat);

#if (VERBOSE >= 1)
    std::cerr << my_time() << " Normal exit" << std::endl;
#endif
    return 0;
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
    std::cerr << "Usage: tcppipe <first_spec> <second_spec>" << std::endl;
    std::cerr << "Each of <first_spec> and <second_spec> can be one of" <<
        std::endl;
    std::cerr << "    -stdio" << std::endl;
    std::cerr << "    -listen <port_number>" << std::endl;
    std::cerr << "    -listen <address>:<port_number>" << std::endl;
    std::cerr << "    -connect <hostname>:<port_number>" << std::endl;
    exit (1);
}

void copy(int firstFD, int secondFD, const std::atomic<bool>& cflag)
{
    if (firstFD  == -1) firstFD  = 0;
    if (secondFD == -1) secondFD = 1;
    set_flags(firstFD , O_NONBLOCK);
    set_flags(secondFD, O_NONBLOCK);
    try
    {
#if (VERBOSE >= 2)
        std::cerr << my_time() << " starting copy, FD " << firstFD <<
            " to FD " << secondFD << std::endl;
        auto stats = copyfd_while(
            firstFD, secondFD, cflag, 500000, 128*1024);
        std::cerr << stats.bytes_copied << " bytes, " <<
            stats.reads << " reads, " <<
            stats.writes << " writes." << std::endl;
#else
        copyfd_while(
            firstFD, secondFD, cflag, 500000, 128*1024);
#endif
    }
    catch (const ReadException& r)
    {
        std::cerr << "Read failure after " << r.byte_count << " bytes:" <<
            std::endl;
        std::cerr << strerror(r.errn) << std::endl;
    }
    catch (const WriteException& w)
    {
        std::cerr << "Write failure after " << w.byte_count << " bytes:" <<
            std::endl;
        std::cerr << strerror(w.errn) << std::endl;
    }
}
