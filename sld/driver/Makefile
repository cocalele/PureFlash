MODULE_NAME := s5bd
$(MODULE_NAME)-objs:= s5k_miscdev.o s5k_blkdev.o s5k_imagectx.o s5k_conductor.o
EXTRA_CFLAGS=-I$(PWD)/../include 
EXTRA_CFLAGS += -D _S5BD_KERNEL_
CURRENT = $(shell uname -r)
KDIR = /lib/modules/$(CURRENT)/build/

obj-m :=s5bd.o

SRCS = s5k_miscdev.c s5k_blkdev.c s5k_imagectx.c s5k_conductor.c

OBJS = $(SRCS:.c=.o)
ccflags-y += -g

default:
	$(MAKE) -C $(KDIR) M=`pwd` modules
clean:
	$(MAKE) -C $(KDIR) M=`pwd` clean
	$(RM) Module.markers modules.order *.o

