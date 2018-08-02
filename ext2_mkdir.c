#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <memory.h>
#include "ext2.h"

#define SINGLE_INDIRECT 12

unsigned char *disk;


/*
 * This program make an directory at the specific absolute path.
 */
int main (int argc, char **argv) {

    // Check valid command line arguments
    if (argc != 3) {
        printf("Usage: ext2_mkdir <virtual_disk> <absolute_path>\n");
        exit(1);
    }

    unsigned char *disk = get_disk_loc(argv[1]);
    // struct ext2_group_desc *gd = get_group_descriptor_loc(disk);
    struct ext2_super_block *sb = get_superblock_loc(disk);
    struct ext2_inode *i_table = get_inode_table_loc(disk);
    unsigned char *i_bitmap = get_inode_bitmap_loc(disk);
    // unsigned char *b_bitmap = get_block_bitmap_loc(disk);

    // Check valid target absolute_path
    // If not valid -> ENOENT
    // Get the inode of the given path
    struct ext2_inode *target_inode = trace_path(argv[2], disk);
    if (target_inode != NULL) { // Target path exists
        printf("ext2_mkdir: %s :Directory exists.\n", argv[2]);
        return EEXIST;
    }
    char *parent_path = get_dir_parent_path(argv[2]);
    struct ext2_inode *parent_inode = trace_path(parent_path, disk);
    if (parent_inode == NULL) {
        printf("ext2_mkdir: %s :Invalid path.\n", argv[2]);
        return ENOENT;
    }

    if (strlen(get_file_name(argv[2])) > EXT2_NAME_LEN) {
        printf("ext2_mkdir: Target directory with name too long: %s\n", get_file_name(argv[2]));
        return ENOENT;
    }

    // Check if there is enough inode (require 1)
    if (sb->s_free_inodes_count <= 0 || sb->s_free_blocks_count <= 0) {
        printf("ext2_mkdir: File system does not have enough free inodes or blocks.\n");
        return ENOSPC;
    }

    // Require a free inode
    int i_num = get_free_inode(disk, i_bitmap);
    struct ext2_inode *tar_inode = &(i_table[i_num - 1]);

    // Init the inode
    tar_inode->i_mode = EXT2_S_IFREG;
    tar_inode->i_size = EXT2_BLOCK_SIZE;
    tar_inode->i_links_count = 0;
    tar_inode->i_blocks = 0;

    if (add_new_entry(disk, tar_inode, (unsigned int)i_num, get_file_name(argv[2]), 'd') == -1) {
        printf("ext2_mkdir: Fail to add new directory entry in directory: %s\n", argv[3]);
        exit(0);
    }

    if (add_new_entry(disk, tar_inode, (unsigned int)get_inode_num(disk, parent_inode), get_file_name(parent_path), 'd') == -1) {
        printf("ext2_mkdir: Fail to add new directory entry in directory: %s\n", argv[3]);
        exit(0);
    }

    // Create a new entry in directory
    if (add_new_entry(disk, parent_inode, (unsigned int)i_num, get_file_name(argv[2]), 'd') == -1) {
        printf("ext2_mkdir: Fail to add new directory entry in directory: %s\n", argv[3]);
        exit(0);
    }
    return 0;
}
