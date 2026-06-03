SHELL := /bin/bash
PWD := $(shell pwd)

CC := /usr/bin/gcc
CFLAGS := -I$(PWD)/
CFLAGS_1 := -no-pie -fno-pie -fno-pic -fcf-protection=none -g -I$(PWD)/

UL_SRC = $(wildcard $(PWD)/*.c)
UL_OBJ = $(patsubst %.c, %.o, $(UL_SRC))

EXEC := ud2_test
EXEC1 := init_eptp_contexts
EXEC2 := pt_test
EXEC3 := micro_vmcall
EXEC4 := micro_empty
EXEC5 := micro_ud2
EXEC6 := micro_nmpmi
EXEC7 := micro_objlog
EXEC8 := statistics
EXEC9 := init_obj_contexts
EXEC10 := pin_memory
EXEC11 := activate_eptp_contexts
EXEC12 := lbr_test

all: $(EXEC) $(EXEC1) $(EXEC2) $(EXEC3) $(EXEC4) $(EXEC5) $(EXEC6) $(EXEC7) $(EXEC8) $(EXEC9) $(EXEC10) $(EXEC11) $(EXEC12)

$(EXEC): ud2_test.c ud2_page.S
	$(CC) $(CFLAGS_1) -o $@ $^

$(EXEC1): init_eptp_contexts.c
	$(CC) $(CFLAGS) -o $@ $^

$(EXEC2): pt_test.c
	$(CC) $(CFLAGS) -o $@ $^

$(EXEC3): micro_vmcall.c
	$(CC) $(CFLAGS) -o $@ $^

$(EXEC4): micro_empty.c
	$(CC) $(CFLAGS) -o $@ $^

$(EXEC5): micro_ud2.c ud2_page.S
	$(CC) $(CFLAGS) -o $@ $^

$(EXEC6): micro_nmpmi.c
	$(CC) $(CFLAGS) -o $@ $^

$(EXEC7): micro_objlog.c
	$(CC) $(CFLAGS) -o $@ $^

$(EXEC8): statistics.c
	$(CC) $(CFLAGS) -o $@ $^

$(EXEC9): init_obj_contexts.c
	$(CC) $(CFLAGS) -o $@ $^

$(EXEC10): pin_memory.c
	$(CC) $(CFLAGS) -o $@ $^

$(EXEC11): activate_eptp_contexts.c
	$(CC) $(CFLAGS) -o $@ $^

$(EXEC12): lbr_test.c
	$(CC) $(CFLAGS) -o $@ $^

obj-m += cf_logging_enable.o
KVERN := $(shell uname -r)

module:
	make CC=$(CC) -C /lib/modules/$(KVERN)/build M=$(PWD) modules
	rm -rf *.mod* .*.cmd *.symvers *.order *.o

obj-m += walk_memory.o

mem:
	make CC=$(CC) -C /lib/modules/$(KVERN)/build M=$(PWD) modules
	rm -rf *.mod* .*.cmd *.symvers *.order *.o

clean:
	rm -rf *.o $(EXEC)
	rm -rf *.o $(EXEC1)
	rm -rf *.o $(EXEC2)
	rm -rf *.o $(EXEC3)
	rm -rf *.o $(EXEC4)
	rm -rf *.o $(EXEC5)
	rm -rf *.o $(EXEC6)
	rm -rf *.o $(EXEC7)
	rm -rf *.o $(EXEC8)
	rm -rf *.o $(EXEC9)
	rm -rf *.o $(EXEC10)
	rm -rf *.o $(EXEC11)
	rm -rf *.o $(EXEC12)
	rm -rf *.out
	make CC=$(CC) -C /lib/modules/$(KVERN)/build M=$(PWD) clean
