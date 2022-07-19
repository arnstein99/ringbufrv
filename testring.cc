#include <cstdlib>
#include <cassert>
#include <cstring>
#include <iostream>
#include <errno.h>
#include <unistd.h>
#include <string>
#include <thread>
#include <stdio.h>
#include <chrono>
#include <mutex>
using namespace std::chrono_literals;
#include "ringbufr.h"
#include "miscutils.h"

// Tuning
static const int read_usleep_range  = 50000;
static const int write_usleep_range = 50000;
static const size_t push_pad = 7;
static const size_t pop_pad = 6;
static const size_t ring_size = 37;
static const size_t verbose = 1;
#define DEFAULT_RUN_SECONDS 300

static std::mutex ringMutex;
int last_read_value, last_write_value;

void itoa(int num, char* str)
{
    sprintf(str, "%d", num);
}
class TestClass
{
public:
    TestClass(int serial=0)
     : serialNumber(serial)
    {
        name = new char[9];
        itoa(serial, name);
    }
    ~TestClass()
    {
        delete[] name;
    }
    TestClass(const TestClass&) = delete;
    TestClass& operator=(const TestClass&) = delete;
    TestClass& operator=(TestClass&& other)
    {
        serialNumber = other.serialNumber;
        auto tmp = name;
        name = other.name;
        other.name = tmp;
        return *this;
    }
    bool operator==(const TestClass& other)
    {
        return
            (serialNumber == other.serialNumber) &&
            (strcmp(name, other.name) == 0);
    }
    TestClass& operator=(int ser)
    {
        serialNumber = ser;
        itoa(ser, name);
        return *this;
    }
    bool operator!=(const TestClass& other)
    {
        return !operator==(other);
    }
    std::ostream& print(std::ostream& ost) const
    {
        ost << serialNumber << " (\"" << name << ")\"";
        return ost;
    }
    int serial() const
    {
        return serialNumber;
    }
    const char* desc() const
    {
        return name;
    }
private:
    int serialNumber;
    char* name;
};
std::ostream& operator<<(std::ostream& ost, const TestClass& tc)
{
    return tc.print(ost);
}
template <>
void RingbufR<TestClass>::validate(const TestClass* start, size_t count)
{
    if (count == 0) return;
    int serial = start->serial();
    int converted = mstoi(start->desc());
    assert(converted == serial);
    for (size_t index = 1 ; index < count ; ++index)
    {
        assert (++serial == start[index].serial());
        converted = mstoi(start[index].desc());
        assert(converted == serial);
    }
}

const TestClass* buffer;

int my_rand(int lower, int upper)
{
    if (lower == upper) return lower;
    return lower + rand() % (upper - lower);
}

static RingbufR<TestClass> rbuf (
    ring_size + push_pad + pop_pad, push_pad, pop_pad);
static bool running = true;

static void Reader ();
static void Writer ();
static void Usage_exit (int exit_val);

int main (int argc, char* argv[])
{
    int run_seconds;
    switch (argc)
    {
    case 1:
        run_seconds = DEFAULT_RUN_SECONDS;
        break;
    case 2:
        if (sscanf (argv[1], "%d", &run_seconds) != 1)
        {
            std::cerr << "Illegal numeric expression \"" << argv[1] <<
                         "\"" << std::endl;
            exit (1);
        }
        if (run_seconds < 0)
        {
            std::cerr << "Please enter a non-negative number or nothing" <<
                         std::endl;
            Usage_exit (1);
        }
        break;
    default:
        run_seconds = 0; // silence compiler warning
        Usage_exit (0);
        break;
    }
    // Cheat
    buffer = rbuf.buffer_start();

    std::thread hReader (Reader);
    std::thread hWriter (Writer);
    sleep (run_seconds);

    running = false;
    hWriter.join();
    hReader.join();
    assert (last_read_value == last_write_value);
}

static void Writer ()
{
    static __thread int serial = 0;

    while (running)
    {
        int write_usleep = my_rand(1, write_usleep_range);
        std::this_thread::sleep_for(write_usleep * 1us);
        size_t available;
        TestClass* start;
        const std::lock_guard<std::mutex> lock(ringMutex);
        rbuf.pushInquire(available, start);
        size_t expected_available = std::min(
            push_pad,
            ring_size - std::min(ring_size, rbuf.size()));
        if (available < expected_available)
        {
            std::cerr << "DEFECT: push: " << available << " < " <<
                expected_available << std::endl;
            exit(1);
        }
        write_usleep = my_rand(1, write_usleep_range);
        std::this_thread::sleep_for(write_usleep * 1us);
        if (available)
        {
            size_t count = my_rand(1, available);
            if (verbose >= 1)
            {
                std::cout << "(will push " << count <<
                    "/" << available <<
                    " at " << start - buffer <<
                    " starting with value " << serial + 1 << ")" << std::endl;
            }
            size_t i = count;
            while (i-- > 0)
            {
                *start++ = ++serial;
            }
            try
            {
                rbuf.push(count);
            }
            catch (RingbufRFullException)
            {
                std::cout << "Write failure" << std::endl;
                exit(1);
            }
            last_write_value = serial;
            std::cout << "size is now " << rbuf.size() <<
                ", ring is (" << rbuf.ring_start() - buffer <<
                "," << rbuf.ring_end() - buffer << ")" <<std::endl;
        }
        else
        {
            assert (rbuf.size() >= ring_size);
            std::cout << "(will push 0 (buffer is full))" << std::endl;
        }
    }
}


static void Reader ()
{
    static __thread int serial = 0;

    while (true)
    {
        size_t read_usleep = my_rand(1, read_usleep_range);
        std::this_thread::sleep_for(read_usleep * 1us);
        size_t available;
        TestClass* start;
        const std::lock_guard<std::mutex> lock(ringMutex);
        rbuf.popInquire(available, start);
        size_t expected_available = std::min(
            pop_pad,
            std::min(ring_size, rbuf.size()));
        if (available < expected_available)
        {
            std::cerr << "DEFECT: pop: " << available << " < " <<
                expected_available << std::endl;
            exit(1);
        }
        read_usleep = my_rand(1, read_usleep_range);
        std::this_thread::sleep_for(read_usleep * 1us);
        if (available)
        {
            size_t count = my_rand(1, available);
            if (verbose >= 1)
                std::cout << "(will pop " << count <<
                "/" << available <<
                " starting at " << start - buffer << ")" << std::endl;
            for (size_t i = 0 ; i < count ; ++i)
            {
                ++serial;
                if (start->serial() != serial)
                {
                    std::cout << "*** ERROR *** ";
                    std::cout << "Pop: expected " << serial << " got " <<
                        start->serial() << " offset " << i << std::endl;
                    exit(1);
                }
                int converted = mstoi(start->desc());
                if (converted != serial)
                {
                    std::cout << "*** ERROR *** ";
                    std::cout << "Pop: corrupted entry at offset " << i;
                    std::cout << "seq " << serial << " but desc \"";
                    std::cout << start->desc() << "\"" << std::endl;
                }
                ++start;
            }
            try
            {
                rbuf.pop(count);
            }
            catch (RingbufREmptyException)
            {
                std::cout << "Read failure " << std::endl;
                exit(1);
            }
            last_read_value = serial;
            std::cout << "size is now " << rbuf.size() <<
                ", ring is (" << rbuf.ring_start() - buffer <<
                "," << rbuf.ring_end() - buffer << ")" <<std::endl;
        }
        else
        {
            assert(rbuf.size() == 0);
            std::cout << "(will pop 0 (buffer is empty))" << std::endl;
            if (!running)
            {
                break;
            }
        }
    }
}

static void Usage_exit (int exit_val)
{
    std::cerr << "Usage: test_ring [run_seconds]" << std::endl;
    exit (exit_val);
}

#include "ringbufr.tcc"
