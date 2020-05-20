obj-m := assoofs.o

all: ko mkassoofs

ko:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

mkassoofs_SOURCES:
	mkassoofs.c assoofs.h

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm mkassoofs