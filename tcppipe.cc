#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <thread>
#include <mutex>
#include <iostream>
#include <cstring>
#include <list>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "copyfd.h"
#include "miscutils.h"
#include "netutils.h"
// Debug code
#include <cassert>

struct Uri
{
    bool listening;
    int port;                // -1 means stdin or stdout
    std::string hostname;    // Not always defined
};
static Uri process_args(int& argc, char**& argv);

static unsigned serial = 0;
static unsigned last_serial = 0;
struct ThreadRecord
{
    unsigned serial;
    std::thread id;
    bool running;
};
static void cleanup(std::list<ThreadRecord*>& records);

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

static void handle_clients(Int2 sck, const Uri* ur);
static void copy(int firstFD, int secondFD, const std::atomic<bool>& cflag);
static void usage_error();

std::mutex mtx;

int main(int argc, char* argv[])
{
    // Process user inputs
    int argc_copy = argc - 1;
    char** argv_copy = argv;
    ++argv_copy;
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
            NEGCHECK("listen", listen(listener[index], 10));
            // Debug code
            std::cerr << "listen(" << listener[index] << ")" <<
                std::endl;
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
    std::list<ThreadRecord*> threads;
    std::thread* last_thread;

    // Loop over clients
    do
    {
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
            auto accept_if =
                    [&uri, &listener, &accepted_sock] (int index)
            {
                if (uri[index].listening)
                {
                    accepted_sock[index] = get_client(listener[index]);
                }
            };
            // Debug code
            std::cerr << "waiting on accept(s) ..." << std::flush;
            accept_if(0);
            accept_if(1);
            std::cerr << std::endl;
        }

        auto* record = new ThreadRecord;
        record->serial = ++serial;
        last_serial = record->serial;
        record->running = true;
        // Debug code
        std::cerr << "create " << record->serial << 
            " -----------------------------" << std::endl;
        auto responder =
            [record, accepted_sock, uri]()
            {
                int final_sock[2] = {-1, -1};
                // Debug code
                std::cerr << "run " << record->serial << 
                    " " << std::this_thread::get_id() <<
                    " -----------------------------" << std::endl;
                std::cerr << "in responder lambda with accepted_sock=[" <<
                    accepted_sock[0] << "," << accepted_sock[1] << "]" <<
                    std::endl;
                auto connect_if =
                        [&uri, &accepted_sock, &final_sock] (int index)
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
                                    uri[index].hostname, uri[index].port, true);
                            if (final_sock[index] == -1)
                            {
                                errorexit("ERROR: connect to listener");
                            }
                        }
                    }
                };
                connect_if(0);
                connect_if(1);
                // Debug code
                std::cerr << my_time() << " Calling handle_clients() " <<
                    final_sock[0] << " <--> " << final_sock[1] << std::endl;
                handle_clients(final_sock, uri);
                // Debug code
                std::cerr << "mutex 1 ..." << std::flush;
                {   auto lock2 = std::lock_guard(mtx);
                    std::cerr << std::endl;
                    record->running = false;
                }
                // Debug code
                std::cerr << "De-run " << record->serial << 
                    " " << std::this_thread::get_id() <<
                    " -----------------------------" << std::endl;
            };
        record->id = std::thread(responder);
        last_thread = &record->id;
        // Debug code
        std::cerr << "mutex 2" << std::flush;
        {   auto lock1 = std::lock_guard(mtx);
            std::cerr << std::endl;
            cleanup(threads);
            threads.push_back(record);
       }
#if (VERBOSE >= 2)
        std::cerr << my_time() << " End copy loop" << std::endl;
#endif
    } while (repeat);
    (*last_thread).join();

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

void cleanup(std::list<ThreadRecord*>& records)
{
    // Debug code
    std::cerr << "Enter cleanup with " << records.size() << " records" <<
        std::endl;
    for (auto iter = records.begin() ; iter != records.end() ; )
    {
        auto rec = *iter;
        if (rec->running)
        {
            ++iter;
        }
        else
        {
            std::cerr << "clean one " << rec->serial <<
                " -----------------------------" << std::endl;
            assert(rec->id.joinable());
            rec->id.join();
            // Debug code
            std::cerr << "mutex 3 ..." << std::flush;
            std::cerr << "clean two " << rec->serial <<
                " -----------------------------" << std::endl;
            delete rec;
            iter = records.erase(iter);
        }
    }
}

void handle_clients(Int2 sck, const Uri* ur)
{
#if (VERBOSE >= 2)
    std::cerr << my_time() << " Begin copy loop " << sck[0] << " <--> " <<
        sck[1] << std::endl;
#endif
    std::atomic<bool> continue_flag = true;

    auto proc1 = [&ur, &sck, &continue_flag] ()
    {
        if (ur[0].port == -1) sck[0] = 0;
        if (ur[1].port == -1) sck[1] = 1;
        copy(sck[0], sck[1], continue_flag);
        continue_flag = false;
    };
    std::thread one(proc1);
    auto proc2 = [&ur, &sck, &continue_flag] ()
    {
        if (ur[0].port == -1) sck[0] = 1;
        if (ur[1].port == -1) sck[1] = 0;
        copy(sck[1], sck[0], continue_flag);
        continue_flag = false;
    };
    std::thread two(proc2);

    // Debug code
    std::cerr << "sub-join 1" << std::endl;
    two.join();
    std::cerr << "sub-join 2" << std::endl;
    one.join();
    std::cerr << "sub-join 3" << std::endl;

    for (size_t index = 0 ; index < 2 ; ++index)
    {
#if (VERBOSE >= 1)
        std::cerr << my_time() << " closing socket " << sck[index] <<
            std::endl;
#endif
        if (sck[index] > 1)
            close(sck[index]);
    }
    // Debug code
    std::cerr << "Leaving handle_clients()" << std::endl;
}

void copy(int firstFD, int secondFD, const std::atomic<bool>& cflag)
{
    set_flags(firstFD , O_NONBLOCK);
    set_flags(secondFD, O_NONBLOCK);
    // Debug code
    std::cerr << "no-block on " << firstFD << ", " << secondFD << std::endl;
    try
    {
#if (VERBOSE >= 2)
        std::cerr << my_time() << " starting copy, FD " << firstFD <<
            " to FD " << secondFD << std::endl;
        auto stats = copyfd_while(
            firstFD, secondFD, cflag, 500000, 4*1024);
        std::cerr << stats.bytes_copied << " bytes, " <<
            stats.reads << " reads, " <<
            stats.writes << " writes." << std::endl;
#else
        copyfd_while(
            firstFD, secondFD, cflag, 500000, 4*1024);
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
