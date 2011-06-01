#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int
main(int argint, char *args[])
{
	int n;
	int fda, fdb, fdc;
	if ((fda = open("a.txt",O_RDWR)) == -1) {
		printf(1,"Error opening a.txt\n");
		exit();
	}
	char abuf[6];
	while((n = read(fda, abuf, sizeof(abuf))) > 0)
	write(1, abuf, n);
	close(fda);
	if ((fdb = open("b.txt",O_RDWR)) == -1) {
		printf(1,"Error opening b.txt\n");
		exit();
	}
	char bbuf[600];
	while((n = read(fdb, bbuf, sizeof(bbuf))) > 0)
	write(1, bbuf, n);
	close(fdb);
	if ((fdc = open("c.txt",O_RDWR)) == -1) {
		printf(1,"Error opening c.txt\n");
		exit();
	}
	char cbuf[6000];
	while((n = read(fdc, cbuf, sizeof(cbuf))) > 0)
	write(1, cbuf, n);
	close(fdc);
	exit();
}
