#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <assert.h>
#include <string.h>

#include "helper.h"

/**
 * This program works like mkdir, creating the final directory on the
 * specified path on the disk.
 *
 * @arg1: An ext2 formatted virtual disk
 * @arg2: An absolute path on this disk
 *
 * @return:        Success:                    0
 *                 Path not exist:             ENOENT
 *                 Directory already exists:     EEXIST
 */
int main(int argc, char **argv){
    //check input
    if(argc != 3) {
        fprintf(stderr, "Usage: %s <image file name> <directory path>\n", argv[0]);
        exit(1);
    }
    
    if (strcmp("/", argv[2]) == 0) {
        fprintf(stderr, "Directory already exists\n");
        return EEXIST;
    }
    
    return 0;
}
