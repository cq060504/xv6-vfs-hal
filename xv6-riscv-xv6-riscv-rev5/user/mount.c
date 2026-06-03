#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if(argc != 4){
    fprintf(2, "Usage: mount path dev fstype\n");
    exit(1);
  }
  int dev = atoi(argv[2]);
  if(mount(argv[1], dev, argv[3]) < 0){
    fprintf(2, "mount: failed\n");
    exit(1);
  }
  exit(0);
}
