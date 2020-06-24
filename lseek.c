#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#define SIZE 20

int main() {
	char rbuf[SIZE];
	int ret = 0;

        int fd = open("/dev/zhang_cdev", O_RDWR);
        if(fd == -1) {
             printf("open error\n");
             return -1;
        }

	memset(rbuf, 0, SIZE);
	ret = read(fd, rbuf, 7);
	printf("read : count = %d, %s\n", ret, rbuf);
	lseek(fd, 2, SEEK_SET);
	ret = read(fd, rbuf, 7);
	printf("read : count = %d, %s\n", ret, rbuf);

        close(fd);
        return 0;
}

