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

void remove_dir(unsigned char *, char *);
void clear_directory_content(unsigned char *, int, char *);

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

    // The path do not exist
    if (path_inode == NULL) {
        printf("The path %s do not exist.\n", argv[2]);
        return ENOENT;
    }

    // Cannot delete root
    if (strcmp(argv[2], "/") == 0) {
        printf("User cannot delete the root dir.\n");
        exit(1);
    }

    // Is a file or link
    if ((path_inode->i_mode & EXT2_S_IFREG) || (path_inode->i_mode & EXT2_S_IFLNK)) {
        remove_file_or_link(disk, argv[2]);
    } else if (path_inode->i_mode & EXT2_S_IFDIR) { // Is a directory
        remove_dir(disk, argv[2]);
    }

    return 0;
}


/*
 * Remove the directory of given path.
 */
void remove_dir(unsigned char *disk, char *path) {
    struct ext2_super_block *sb = get_superblock_loc(disk);
    struct ext2_group_desc *gd = get_group_descriptor_loc(disk);
    unsigned char *block_bitmap = get_block_bitmap_loc(disk);
    struct ext2_inode *path_inode = trace_path(path, disk);

    int block_num = path_inode->i_block[0];

    // remove all the contents inside the dir, avoid . and ..
    for (int i = 0; i < SINGLE_INDIRECT; i++) {
        if (path_inode->i_block[i]) { // has data in the block
            clear_directory_content(disk, path_inode->i_block[i], path);
            block_num = path_inode->i_block[i];
        }
    }

    if (path_inode->i_block[SINGLE_INDIRECT]) {
        unsigned int *indirect = get_indirect_block_loc(disk, path_inode);

        for (int j = 0; j < EXT2_BLOCK_SIZE / sizeof(unsigned int); j++) {
            if (indirect[j]) {
                clear_directory_content(disk, indirect[j], path);
                block_num = indirect[j];
            }
        }
    }

    // zero the block bitmap and inode bitmap
    zero_bitmap(block_bitmap, block_num);
    sb->s_free_blocks_count++;
    gd->bg_free_blocks_count++;
    clear_inode_bitmap(disk, path_inode);

    // get the parent directory
    char *parent_path = get_dir_parent_path(path);
    struct ext2_inode *parent_dir = trace_path(parent_path, disk);
    parent_dir->i_links_count--;
    parent_dir->i_blocks-=path_inode->i_blocks;
    // remove current directory's name but keep the inode
    remove_name(disk, path);
    gd->bg_used_dirs_count--;

    // update the field of removed dir inode
    path_inode->i_block[block_num] = 0;
    path_inode->i_dtime = (unsigned int) time(NULL);
    path_inode->i_size = 0;
    path_inode->i_blocks = 0;
}

/*
 * Clear all the entries in one block.
 */
void clear_directory_content(unsigned char *disk, int block_num, char *path) {
    struct ext2_dir_entry_2 *dir = get_dir_entry(disk, block_num);
    struct ext2_dir_entry_2 *pre_dir = NULL;
    int curr_pos = 0; // used to keep track of the dir entry in each block

    while (curr_pos < EXT2_BLOCK_SIZE) {
        dir->name[dir->name_len] = '\0';
        int rec_len = dir->rec_len;

        if (strcmp(dir->name, ".") != 0
            && strcmp(dir->name, "..") != 0) {
            rec_len = dir->rec_len;
            if ((dir->file_type == EXT2_FT_REG_FILE)
                || (dir->file_type == EXT2_FT_SYMLINK)) {
                remove_file_or_link(disk, combine_name(path, dir));
            } else if (dir->file_type == EXT2_FT_DIR) {
                remove_dir(disk, combine_name(path, dir));
            }
            pre_dir->rec_len += dir->rec_len;
        }

        /* Moving to the next directory */
        curr_pos = curr_pos + rec_len;
        pre_dir = dir;
        dir = (void*) dir + rec_len;
    }
}
