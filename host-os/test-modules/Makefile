obj-m += lbr_test.o

PWD := $(CURDIR)
KVERN := $(shell uname -r)

all:
	        make CC=gcc -C /lib/modules/$(KVERN)/build M=$(PWD) modules
			rm -rf *.mod* .*.cmd *.symvers *.order *.o
clean:
	        make CC=gcc -C /lib/modules/$(KVERN)/build M=$(PWD) clean
