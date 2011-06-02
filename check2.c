#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"


int
main(int argint, char *args[])
{
	int bytes_num = atoi(args[1]);
	int n, fda, fdb, fdc, finisha, finishb, finishc;


	if (argint != 2) {
          printf(1, "USAGE: check2 <bytes_num>\n");
	  exit();
        }

	finisha = 0;
	finishb = 0;
	finishc = 0;

	if ((fda = open("a.txt",O_RDWR)) == -1) {
		printf(1,"Error opening a.txt\n");
		exit();
	}
	if ((fdb = open("b.txt",O_RDWR)) == -1) {
		printf(1,"Error opening b.txt\n");
		exit();
	}
	if ((fdc = open("c.txt",O_RDWR)) == -1) {
		printf(1,"Error opening c.txt\n");
		exit();
	}
	char buf[bytes_num];
	while(finisha == 0 || finishb == 0 || finishc ==0) {
		if(finisha == 0) {
			n = read(fda, buf, bytes_num);
			//write(1, buf, n);
			if (n == 0) {
				finisha = 1;
				close(fda);
			}
		}
		if(finishb == 0) {
			n = read(fdb, buf, bytes_num);
			//write(1, buf, n);
			if (n == 0) {
				finishb = 1;
				close(fdb);
			}
		}
		if(finishc == 0) {
			n = read(fdc, buf, bytes_num);
			//write(1, buf, n);
			if (n == 0) {
				finishc = 1;
				close(fdc);
			}
		}
	}
	exit();
}
