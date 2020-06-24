#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

int main() {
        char * buf = "abcdefg";
	int ret = 0;

        int fd = open("/dev/zhang_cdev", O_RDWR);
        if(fd == -1) {
             printf("open error\n");
             return -1;
        }

	printf("write : %s\n", buf);
        ret = write(fd, buf, 7);
	printf("write : count = %d\n", ret);

	lseek(fd, 3, SEEK_SET);
	printf("write : %s  -- begin pos = 3\n", buf);
        ret = write(fd, buf, 7);
	printf("write : count = %d\n", ret);

        close(fd);
        return 0;
}

