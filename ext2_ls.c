#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
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

    // Get the inode of the given path
    struct ext2_inode *path_inode = trace_path(argv[2], disk);

    // How to get a ext2_dir_entry_2 * !!!!!!!!!!!!!!!!!!!!!!
    struct ext2_dir_entry_2 *dir = NULL;

    char type = '\0';
    if (path_inode != NULL) { // the given path exists
        // Check the type of inode
        if (path_inode->i_mode & EXT2_S_IFREG || path_inode->i_mode & EXT2_S_IFLNK) { // File or link
            type = 'f';
        } else if (path_inode->.i_mode & EXT2_S_IFDIR) { // Directory
            type = 'd';
        }

        if (argc == 3) {
            if (type == 'f') { // Only print file or link name
                printf("%s\n", get_file_name(argv[2]));
            } else if (type == 'd') { // Print all entries in the directory
                print_entries(disk, dir, NULL);
            }

        } else { // "-a" case
            if (type == 'f') { // Refrain from printing the . and ..
                printf("%s\n", get_file_name(argv[2]));
            } else if (type == 'd') { // Print all entries in the directory as well as . and ..
                print_entries(disk, dir, argv[3]);
            }
        }

    } else { // the given path does not exist
        printf("No such file or directory.\n");
        return ENOENT;
    }

    return 0;


}

