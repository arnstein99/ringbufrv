# Tuning
ifeq ($(strip $(OPT)),)
    CCFLAGS += -g
    LDFLAGS += -g
else
    CCFLAGS += $(OPT)
    LDFLAGS += $(OPT)
endif
ifneq ($(strip $(NDEBUG)),)
    CPPFLAGS += -DNDEBUG=$(NDEBUG)
endif
ifneq ($(strip $(VERBOSE)),)
    CPPFLAGS += -DVERBOSE=$(VERBOSE)
endif

CCFLAGS += -std=c++2a -Wall
LDLIBS += -lpthread
LINK.o = c++ $(LDFLAGS)
COMPILE.cc = c++ -c $(CPPFLAGS) $(CCFLAGS)

all: testring tcpcat tcppipe

testring: testring.o miscutils.o
tcpcat: tcpcat.o copyfd.o miscutils.o netutils.o
tcppipe: tcppipe.o copyfd.o miscutils.o netutils.o

copyfd.o: ringbufr.h ringbufr.tcc
testring.o: ringbufr.h ringbufr.tcc
tcpcat.o: copyfd.h miscutils.h netutils.h
tcppipe.o: copyfd.h miscutils.h netutils.h
netutils.o: netutils.h miscutils.h ringbufr.h
miscutils.o: miscutils.h
