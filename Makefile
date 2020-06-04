obj-m := assoofs.o

all: ko mkassoofs final

ko:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
	
final:
	dd bs=4096 count=100 if=/dev/zero of=image
	mkdir mnt
	./mkassoofs image
	insmod assoofs.ko
	mount -o loop -t assoofs image mnt

mkassoofs_SOURCES:
	mkassoofs.c assoofs.h

clean:
	umount mnt
	rmmod assoofs.ko
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm mkassoofs
	rm image
	rmdir mnt