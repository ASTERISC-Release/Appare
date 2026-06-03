obj-m += objprofile.o

PWD := $(CURDIR)

all:
	            make CC=gcc -C /lib/modules/6.1.9/build M=$(PWD) modules
clean:
	            make CC=gcc -C /lib/modules/6.1.9/build M=$(PWD) clean
