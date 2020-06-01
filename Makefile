obj-m := assoofs.o

all: ko mkassoofs

ko:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
	dd bs=4096 count=100 if=/dev/zero of=image

mkassoofs_SOURCES:
	mkassoofs.c assoofs.h

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm mkassoofs
	rm -rf mnt
	rm image
