CFLAGS	= $(shell pkg-config --cflags libsystemd) -Wall -O2 -g -fPIC
LDLIBS	= $(shell pkg-config --libs libsystemd)

ifdef KERNEL_HEADERS
	CFLAGS += -I$(KERNEL_HEADERS)
endif

EXE = btbridged

all: $(EXE)

check: $(EXE) ipmi-bouncer libbthost.so
	./run_tests.sh

libbthost.so: bt-host.o
	$(LINK.c) -shared $^ -o $@

clean:
	$(RM) *.o $(EXE) libbthost.so ipmi-bouncer
