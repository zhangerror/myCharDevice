#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <unistd.h>

#define ZHANG_CDEV_IOC_MAGIC  'k'

#define ZHANG_CDEV_IOCTLRESET		_IO(ZHANG_CDEV_IOC_MAGIC,   0)
#define ZHANG_CDEV_IOCTLFIXFRONT	_IO(ZHANG_CDEV_IOC_MAGIC,   1)
#define ZHANG_CDEV_IOCTLFIXREAR		_IO(ZHANG_CDEV_IOC_MAGIC,   2)
#define ZHANG_CDEV_IOCTLRESIZE		_IO(ZHANG_CDEV_IOC_MAGIC,   3)

int main() {
	int fd;
	char buf[20] = {0};
	char * wbuf = "123456";
	fd = open("/dev/zhang_cdev", O_RDWR);
	if(fd == -1) {
			printf("open file error\n");
			return -1;
	}

	printf("ioctl : fix front \n");
	ioctl(fd, ZHANG_CDEV_IOCTLFIXFRONT, 5);
	
	printf("ioctl : fix rear \n");
	ioctl(fd, ZHANG_CDEV_IOCTLFIXREAR, 6);
	
	printf("ioctl : resize \n");
	ioctl(fd, ZHANG_CDEV_IOCTLRESIZE, 2048);

	sleep(10);

	printf("ioctl : clear \n");
	ioctl(fd, ZHANG_CDEV_IOCTLRESET);

	close(fd);

	return 0;
}
