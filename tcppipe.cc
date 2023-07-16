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

private:
    int val[2];
};

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

    bool repeat = uri[0].listening || uri[1].listening;
    std::thread last_client;

    // Loop over clients
    do
    {
        int sock[2] = {0, 0};
        Int2 sock2;

        static auto listen_if = [&uri, &sock, &listener] (int index)
        {
            if (uri[index].listening)
            {
                NEGCHECK("listen", listen(listener[index], 1));
                sock[index] = get_client(listener[index]);
            }
        };

        // Special processing for double listen
        if (uri[0].listening && uri[1].listening)
        {
            // wait for one of the URIs to accept
            get_two_clients(listener, sock);
            // Programming note: one of the sock's is accepted. Both are
            // listening.
        }
        else
        {
            // Wait for client to listening socket, if any.
            listen_if(0);
            listen_if(1);
        }

        sock2 = sock;
        static auto client = [] (int listnr[2], Int2 sck, const Uri* ur)
        {
            std::atomic<bool> continue_flag = true;

            // Special processing for double listen
            if (ur[0].listening && ur[1].listening)
            {
                // Recall that both ports are listening, one is accepted.
                for (size_t index = 0 ; index < 2 ; ++index)
                {
                    if (sck[index] == -1)
                    {
                        sck[index] = get_client(listnr[index]);
                        break;
                    }
                }
            }
            else
            {
                auto connect_if = [ur, &sck] (int index)
                {
                    if (!ur[index].listening)
                    {
                        if (ur[index].port != -1)
                        {
                            sck[index] =
                                socket_from_address(
                                    ur[index].hostname, ur[index].port, true);
                        }
                    }
                };
                // Connect client socket and/or stdio socket, if any.
                connect_if(0);
                if (sck[0] == -1)
                {
                    if (ur[1].port != -1) close(sck[1]);
                    std::cerr << "connect: " << strerror(errno) << std::endl;
                    return;
                }
                connect_if(1);
                if (sck[1] == -1)
                {
                    if (ur[0].port != -1) close(sck[0]);
                    std::cerr << "connect: " << strerror(errno) << std::endl;
                    return;
                }
            }
#if (VERBOSE >= 2)
            std::cerr << my_time() << " Begin copy loop" << std::endl;
#endif

            auto proc1 = [ur, &sck, &continue_flag] ()
            {
                if (ur[0].port == -1) sck[0] = 0;
                if (ur[1].port == -1) sck[1] = 1;
                copy(sck[0], sck[1], continue_flag);
                continue_flag = false;
            };
            std::thread one(proc1);
            auto proc2 = [ur, &sck, &continue_flag] ()
            {
                if (ur[0].port == -1) sck[0] = 1;
                if (ur[1].port == -1) sck[1] = 0;
                copy(sck[1], sck[0], continue_flag);
                continue_flag = false;
            };
            std::thread two(proc2);

            two.join();
            one.join();

            if (ur[1].port != -1) close(sck[1]);
            if (ur[0].port != -1) close(sck[0]);
        };
        last_client = std::thread(client, listener, sock2, uri);
        if (repeat) last_client.detach();
#if (VERBOSE >= 2)
        std::cerr << my_time() << " End copy loop" << std::endl;
#endif

    } while (repeat);
    if (!repeat) last_client.join();

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
    set_flags(firstFD , O_NONBLOCK);
    set_flags(secondFD, O_NONBLOCK);
    try
    {
#if (VERBOSE >= 2)
        std::cerr << my_time() << " starting copy, FD " << firstFD <<
            " to FD " << secondFD << std::endl;
        auto stats = copyfd_while(
            firstFD, secondFD, cflag, 500000, 16*1024);
        std::cerr << stats.bytes_copied << " bytes, " <<
            stats.reads << " reads, " <<
            stats.writes << " writes." << std::endl;
#else
        copyfd_while(
            firstFD, secondFD, cflag, 500000, 16*1024);
#endif
    }
    catch (const ReadException& r)
    {
        std::cerr << "Read failure after " << r.byte_count << " bytes: " <<
            strerror(r.errn) << std::endl;
    }
    catch (const WriteException& w)
    {
        std::cerr << "Write failure after " << w.byte_count << " bytes: " <<
            strerror(w.errn) << std::endl;
    }
}
