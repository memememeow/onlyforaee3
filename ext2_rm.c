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
    if (argc != 3) {
        printf("Usage: ext2_rm <virtual_disk> <absolute_path>\n");
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

    // Is a directory
    if (path_inode->i_mode & EXT2_S_IFDIR) {
        return EISDIR;
    }

    // Get the inode of a file/link which need to be removed
    if (path_inode->i_links_count > 1) {
        // decrement links_count
        path_inode->i_links_count--;
        // remove current file's name but keep the inode
        remove_name(disk, argv[2]);

    } else { // links_count == 1, need to remove the actual file/link
        // zero the block bitmap
        zero_block_bitmap(disk, path_inode);
        // zero the inode bitmap
        zero_inode_bitmap(disk, path_inode);

        // set delete time, in order to reuse inode
        path_inode->i_dtime = (unsigned int) time(NULL);
        path_inode->i_size = 0;
        path_inode->i_blocks = 0;
    }

    return 0;
}
