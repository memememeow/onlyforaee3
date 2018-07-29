#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <errno.h>
// #include "ext2.h"
#include "helper.h"

unsigned char *disk;

int main(int argc, char **argv) {
    // Check valid user input
    if (argc != 4) {
        printf("Usage: ext2_rm_bonus <virtual_disk> <absolute_path> <-r>\n");
        exit(1);
    }

    // Map disk image file into memory
    disk = get_disk_loc(argv[1]);

    // Get the inode of the given path
    struct ext2_inode *path_inode = trace_path(argv[2], disk);

    // The file/lin do not exist
    if (path_inode == NULL) {
        return ENOENT;
    }

    // Is a file or link
    if ((path_inode->i_mode & EXT2_S_IFREG) || (path_inode->i_mode & EXT2_S_IFLNK)) {
        remove_file_or_link(disk, path_inode, argv[2]);
    } else if (path_inode->i_mode & EXT2_S_IFDIR) { // Is a directory
        remove_dir(disk, path_inode, argv[2]);
    }

    return 0;
}

