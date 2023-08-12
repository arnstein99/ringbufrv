#include "commonutils.h"
#include "miscutils.h"
// TODO: fix this hack
extern void usage_error();
#include <cstring>

Uri process_args(int& argc, char**& argv)
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
