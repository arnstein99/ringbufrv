#ifndef __COMMONUTILS_H_
#define __COMMONUTILS_H_

// Software common to programs tcpcat and tcppipe, but not otherwise useful.

#include "netutils.h"
#include <string>
#include <vector>

struct ServerInfo
{
    ServerInfo() : port_num(-1), listener(nullptr) { }
    ~ServerInfo() { if (listener) delete listener; }
    inline bool listening() const { return (listener != nullptr); }
    std::string hostname;    // Not always defined
    int port_num;            // Not defined if listening. -1 indicates stdio.
    Listener* listener;
};

struct Uri
{
    bool listening;
    std::vector<int> ports;  // -1 means stdin or stdout
    std::string hostname;    // Not always defined
};
Uri process_args(int& argc, char**& argv);

#endif // __COMMONUTILS_H_
