obj-m = zhang_cdev.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules

clean:
	rm *.symvers *.order *.mod.c *o
