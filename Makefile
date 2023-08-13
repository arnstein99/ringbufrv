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

SRCS := commonutils.cc copyfd.cc miscutils.cc netutils.cc tcpcat.cc \
    tcppipe.cc testring.cc
PROGS := testring tcpcat tcppipe

all : $(PROGS)
clean :
	$(RM) $(PROGS) $(SRCS:%.cc=%.o) $(SRCS:%.cc=$(DEPDIR)/%.d)
.PHONY: all clean

testring: testring.o miscutils.o
tcpcat: tcpcat.o commonutils.o copyfd.o miscutils.o netutils.o
tcppipe: tcppipe.o commonutils.o copyfd.o miscutils.o netutils.o

# GNU boilerplate {

DEPDIR := .deps
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.d

COMPILE.cc = c++ $(DEPFLAGS) $(CCFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -c

%.o : %.cc
%.o : %.cc $(DEPDIR)/%.d | $(DEPDIR)
	$(COMPILE.cc) $(OUTPUT_OPTION) $<

$(DEPDIR): ; @mkdir -p $@

DEPFILES := $(SRCS:%.cc=$(DEPDIR)/%.d)
$(DEPFILES):

# This should remain at the end of the file
include $(wildcard $(DEPFILES))

# } GNU boilerplate
