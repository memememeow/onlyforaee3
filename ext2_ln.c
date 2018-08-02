#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include "ext2.h"

#define SINGLE_INDIRECT 12

unsigned char *disk;


/*
 * This program created a linked file from first specific file to second absolute
 * path.
 */
int main (int argc, char **argv) {
    // printf("%s\n", argv[2]);
    // printf("%s\n", argv[3]);
    // printf("%s\n", argv[4]);

    // Check valid command line arguments
    if (argc != 4 && argc != 5) {
        printf("Usage: ext2_ln <virtual_disk> <source_file> <target_file> <-s>\n");
        exit(1);
    } else if (argc == 5 && strcmp(argv[4], "-s") != 0) {
        printf("Usage: ext2_ln <virtual_disk> <source_file> <target_file> <-s>\n");
        exit(1);
    }

    // Check valid disk
    // Open the disk image file.
    unsigned char *disk = get_disk_loc(argv[1]);
    // printf("Disk open\n");

    // Find group descriptor.
    struct ext2_group_desc *gd = get_group_descriptor_loc(disk);

    // Find super block of the disk.
    struct ext2_super_block *sb = get_superblock_loc(disk);

    unsigned char *i_bitmap = get_inode_bitmap_loc(disk, gd);
    unsigned char *b_bitmap = get_block_bitmap_loc(disk, gd);

    // Inode table
    struct ext2_inode *i_table = get_inode_table_loc(disk, gd);
    // printf("I node\n");

    char *dir_path = get_dir_path(argv[3]); // for target file
    struct ext2_inode *source_inode = trace_path(argv[2], disk);
    struct ext2_inode *target_inode = trace_path(argv[3], disk);
    struct ext2_inode *dir_inode = trace_path(dir_path, disk);
    // printf("Get to trace path\n");

    // If source file does not exist -> ENOENT
    if (source_inode == NULL) {
        printf(ext2_ln: "%s :Invalid path.\n", argv[2]);
        return ENOENT;
    }
    printf("Here\n");

    // If target file exist -> EEXIST
    if (target_inode != NULL) {
        // printf("If target file exist -> EEXIST\n");
        if (target_inode->i_mode & EXT2_S_IFDIR) {
            // printf("If dir\n");
            printf("ext2_ln: %s :Path provided is a directory.\n", argv[3]);
            return EISDIR;
        }
        // printf("No matter what\n");
        printf("ext2_ln: %s :File already exist.\n", argv[3]);
        return EEXIST;
    }

    // Directory of target file DNE
    if (dir_inode == NULL) {
        printf("ext2_ln: %s :Invalid path.\n", dir_path);
        return ENOENT;
    }

    // If source file path is a directory -> EISDIR
    if (source_inode->i_mode & EXT2_S_IFDIR) {
        return EISDIR;
    }

    int target_inode_num = -1;
    const char *source_path = (const char *)argv[2];
    char *target_name = get_file_name(argv[3]);
    if (strlen(target_name) > EXT2_NAME_LEN) {
        printf("ext2_ln: Target file with name too long: %s\n", target_name);
        return ENOENT;
    }
    int path_len = (int)strlen(source_path);
    int blocks_needed = path_len / EXT2_BLOCK_SIZE + (path_len % EXT2_BLOCK_SIZE != 0);

    if (argc == 5) { // Create soft link
        // Check if we have enough space for path if symbolic link is created
        if ((blocks_needed <= 12 && sb->s_free_blocks_count < blocks_needed) ||
            (blocks_needed > 12 && sb->s_free_blocks_count < blocks_needed + 1)) {
            printf("ext2_ln: File system does not have enough free blocks.\n");
            return ENOSPC;
        }

        if ((target_inode_num = get_free_inode(disk, i_bitmap)) == -1) {
            printf("ext2_ln: File system does not have enough free inodes.\n");
            return ENOSPC;
        }

        struct ext2_inode *tar_inode = &(i_table[target_inode_num - 1]);

        // Init the inode
        tar_inode->i_mode = EXT2_S_IFLNK;
        tar_inode->i_size = (unsigned int)path_len;
        tar_inode->i_links_count = 1;
        tar_inode->i_blocks = 0;

        // Write path into target file
        int block_index = 0;
        int indirect_b = -1;

        while (block_index * EXT2_BLOCK_SIZE < path_len) {
            int b_num;
            if (block_index < SINGLE_INDIRECT) {
                b_num = get_free_block(disk, b_bitmap);
                tar_inode->i_block[block_index] = (unsigned int)b_num;
            } else {
                if (block_index == SINGLE_INDIRECT) { // First time access indirect blocks
                    indirect_b = get_free_block(disk, b_bitmap);
                    tar_inode->i_block[SINGLE_INDIRECT] = (unsigned int)indirect_b;
                    tar_inode->i_blocks += 2;
                }
                b_num = get_free_block(disk, b_bitmap);
                unsigned int *indirect_block = (unsigned int *) (disk + indirect_b * EXT2_BLOCK_SIZE);
                indirect_block[block_index - SINGLE_INDIRECT] = (unsigned int)b_num;
            }
            unsigned char *block = disk + b_num * EXT2_BLOCK_SIZE;
            // printf("%c\n", source_path[block_index * EXT2_BLOCK_SIZE]);
            strncpy((char *)block, &source_path[block_index * EXT2_BLOCK_SIZE], EXT2_BLOCK_SIZE);
            tar_inode->i_blocks += 2;
            block_index++;
        }

        // // Debug only
        // if (tar_inode->i_blocks / 2 != blocks_needed) {
        //     printf("Something wrong when write path into blocks!!!!!\n");
        // }

        printf("target_inode_num is :%d\n", target_inode_num);
        if (add_new_entry(disk, dir_inode, (unsigned int)target_inode_num, target_name, 'l') == -1) {
            printf("ext2_ln: Fail to add new directory entry in directory: %s\n", dir_path);
            exit(0);
        }

    } else {
        target_inode_num = get_inode_num(disk, source_inode);
        // printf("target_inode_num is :%d\n", target_inode_num);
        // struct ext2_inode *tar_inode = &(i_table[target_inode_num - 1]);
        source_inode->i_links_count++;

        if (add_new_entry(disk, dir_inode, (unsigned int)target_inode_num, target_name, 'f') == -1) {
            printf("ext2_ln: Fail to add new directory entry in directory: %s\n", dir_path);
            exit(0);
        }
    }

    return 0;
}
