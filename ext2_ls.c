#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "ext2.h"
#include "helper.h"

unsigned char *disk;

int main(int argc, char **argv) {
    // Check valid user input
    if (argc != 3 && argc != 4) {
        printf("Usage: ext2_ls <virtual_disk> <absolute_path> <-a>\n");
        exit(1);
    } else if (argc == 4 && argv[3] != "-a") {
        printf("Usage: ext2_ls <virtual_disk> <absolute_path> <-a>\n");
        exit(1);
    }

    // Map disk image file into memory
    disk = get_disk_loc(argv[1]);

    if (argc == 3) {

    } else {

    }


}

