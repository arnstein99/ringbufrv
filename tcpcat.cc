#include <unistd.h>
#include <fcntl.h>
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

static void usage_error();

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
    int socketFD[2] = {0, 0};

    if (uri[0].listening && uri[1].listening)
    {
        // Special processing for double listen.
        // Wait for both URIs to accept.
        int temp[2];
        temp[0] = socket_from_address("", uri[0].port, false);
        temp[1] = socket_from_address("", uri[1].port, false);
        get_two_clients(temp, socketFD);
        close(temp[0]);
        close(temp[1]);
    }
    else
    {
        // Wait for client to listening socket, if any.
        static auto listen_if = [&uri, &socketFD] (int index)
        {
            if (uri[index].listening)
            {
                int temp = socket_from_address("", uri[index].port, false);
                socketFD[index] = get_client(temp);
                close(temp);
            }
        };
        listen_if(0);
        listen_if(1);

        // Connect client socket and/or pipe socket, if any.
        static auto connect_if = [&uri, &socketFD] (int index)
        {
            if (!uri[index].listening)
            {
                if (uri[index].port == -1)
                {
                    // This covers both stdin and stdout
                    socketFD[index] = index;
                }
                else
                {
                    socketFD[index] =
                        socket_from_address(
                            uri[index].hostname, uri[index].port, true);
                }
            }
        };
        connect_if(0);
        connect_if(1);
    }

    // Modify port properties
    set_flags(socketFD[0], O_NONBLOCK|O_RDONLY);
    set_flags(socketFD[1], O_NONBLOCK|O_WRONLY);

    // Copy!
    try
    {
#if (VERBOSE >= 1)
        std::cerr << "starting copy, socket " << socketFD[0] <<
            " to socket " << socketFD[1] << std::endl;
        auto stats = 
            copyfd(socketFD[0], socketFD[1], 128*1024);
        std::cerr << stats.bytes_copied << " bytes, " <<
            stats.reads << " reads, " <<
            stats.writes << " writes." << std::endl;
#else
        copyfd(socketFD[0], socketFD[1], 128*1024);
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

    if (uri[1].port != -1) close(socketFD[1]);
    if (uri[0].port != -1) close(socketFD[0]);
    return 0;
}

static Uri process_args(int& argc, char**& argv)
// Group can be one of
//     -pipe
//     -listen <port>
//     -listen <hostname>:<port>
//     -connect <hostname> <port>
{
    Uri uri;

    if (argc < 1) usage_error();
    const char* option = argv[0];
    ++argv;
    --argc;

    if (strcmp(option, "-pipe") == 0)
    {
        uri.listening = false;
        uri.port = -1;
    }
    else if (strcmp(option, "-listen") == 0)
    {
        uri.listening = true;
        if (argc < 1) usage_error();
        const char* value = argv[0];
        auto vec = dstrtok(value, ':');
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
        ++argv;
        --argc;
    }
    else if (strcmp(option, "-connect") == 0)
    {
        uri.listening = false;
        if (argc < 1) usage_error();
        const char* value = argv[0];
        --argc;
        ++argv;
        uri.hostname = value;
        if (argc < 1) usage_error();
        uri.port = mstoi(argv[0]);
        --argc;
        ++argv;
    }
    else
    {
        usage_error();
    }
    return uri;
}

void usage_error()
{
    std::cerr << "Usage: tcpcat <input_spec> <output_spec>" << std::endl;
    std::cerr << "Each of <input_spec> and <output_spec> can be one of" <<
        std::endl;
    std::cerr << "    -pipe" << std::endl;
    std::cerr << "    -listen <port_number>" << std::endl;
    std::cerr << "    -listen <hostname>:<port_number>" << std::endl;
    std::cerr << "    -connect <hostname> <port_number>" << std::endl;
    exit (1);
}
