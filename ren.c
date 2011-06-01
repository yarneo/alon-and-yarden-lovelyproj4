#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int
main(int argint, char *args[])
{
	if (argint != 4) {
          printf(1, "USAGE: ren <path> <old_name> <new_name>\n");
	  exit();
        }
	char *path = args[1];
	char *old = args[2];
	char *new = args[3];
	int res = rename(path, old, new);
	if(res == -1) {
	  printf(1, "Error: File does not exsist or invalid path name\n");
        }
	if(res == -2) {
	  printf(1, "Error: File target already exist\n");
        }
	exit();
}
