#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "ext2.h"
#include "helper.h"

#define SINGLE_INDIRECT 12

/*
 * Return the disk location.
 */
unsigned char *get_disk_loc(char *disk_name) {
    int fd = open(disk_name, O_RDWR);

    // Map disk image file into memory
    unsigned char *disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    return disk;
}

/*
 * Return the super block location.
 */
struct ext2_super_block *get_superblock_loc(unsigned char *disk) {
    return (struct ext2_super_block *)(disk + 1024);
}

/*
 * Return the group descriptor location.
 */
struct ext2_group_desc *get_group_descriptor_loc(unsigned char *disk) {
    return (struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE);
}

/*
 * Return the block bitmap (16*8 bits) location.
 */
unsigned char *get_block_bitmap_loc(unsigned char *disk, struct ext2_group_desc *gd) {
    return disk + EXT2_BLOCK_SIZE * (gd->bg_block_bitmap);
}

/*
 * Return the inode bitmap (4*8 bits) location.
 */
unsigned char *get_inode_bitmap_loc(unsigned char *disk, struct ext2_group_desc *gd) {
    return disk + EXT2_BLOCK_SIZE * (gd->bg_inode_bitmap);
}

/*
 * Return the inode table location.
 */
struct ext2_inode  *get_inode_table_loc(unsigned char *disk, struct ext2_group_desc *gd) {
    return (struct ext2_inode *) (disk + EXT2_BLOCK_SIZE * gd->bg_inode_table);
}

/*
 * Return the indirect block location.
 */
unsigned int *get_indirect_block_loc(unsigned char *disk, struct ext2_inode  *inode) {
    return (unsigned int *) (disk + EXT2_BLOCK_SIZE * inode->i_block[SINGLE_INDIRECT]);
}

/*
 * Return the directory location.
 */
struct ext2_dir_entry_2 *get_directory_loc(unsigned char *disk, struct ext2_inode  *inode, int i_block) {
    return (struct ext2_dir_entry_2 *) (disk + EXT2_BLOCK_SIZE * (inode->i_block[i_block]));
}

/*
 * Return the inode of the root directory.
 */
struct ext2_inode *get_root_inode(struct ext2_inode  *inode_table) {
    return &(inode_table[EXT2_ROOT_INO - 1]);
}

/*
 * Trace the given path. Return the inode of the given path.
 */
struct ext2_inode *trace_path(char *path, unsigned char *disk) {
    char *filter = "/";
    unsigned int block_number = 0;
    struct ext2_dir_entry_2 *dir_entry = NULL;
    int size = 0;

    struct ext2_group_desc *gd = get_group_descriptor_loc(disk);
    struct ext2_inode  *inode_table = get_inode_table_loc(disk, gd);

    // Get the inode of the root
    struct ext2_inode *root_inode = get_root_inode(inode_table);

    // Get the copy of the path
    char *full_path = malloc(sizeof(char) * (strlen(path) + 1));
    strncpy(full_path, path, strlen(path));

    char *token = strtok(full_path, filter);
    while (token != NULL) {
        for (int )


        token = strtok(NULL, filter);
    }

}

/*
 * Return the file name of the given valid path.
 */
char *get_file_name(char *path) {
    char *filter = "/";
    char *file_name = malloc(sizeof(char) * (strlen(path) + 1));;

    char *full_path = malloc(sizeof(char) * (strlen(path) + 1));
    strncpy(full_path, path, strlen(path));

    char *token = strtok(full_path, filter);
    while (token != NULL) {
        strncpy(file_name, token, strlen(token));
        token = strtok(NULL, filter);
    }

    return file_name;
}

/*
 * Print all the entries of a given directory inode.
 */
void print_entries(struct ext2_dir_entry_2 *dir, char *flag) {
    int curr_pos = 0;

    if (flag == NULL) { // Not print . and ..
        while (curr_pos < EXT2_BLOCK_SIZE) { // Total size of the entries in a block cannot exceed a block size
            char *print_name = malloc(sizeof(char) * dir->name_len + 1);
            for (int u = 0; u < dir->name_len; u++) {
                print_name[u] = dir->name[u];
            }
            print_name[dir->name_len] = '\0';
            if (strcmp(print_name, ".") != 0 || strcmp(print_name, "..") != 0) {
                printf("%s\n", print_name);
            }
            free(print_name);

            // Move to the next entry
            curr_pos = curr_pos + dir->rec_len;
            dir = (void*) dir + dir->rec_len;
        }

    } else if (flag == "-a") {
        while (curr_pos < EXT2_BLOCK_SIZE) { // Total size of the entries in a block cannot exceed a block size
            char *print_name = malloc(sizeof(char) * dir->name_len + 1);
            for (int u = 0; u < dir->name_len; u++) {
                print_name[u] = dir->name[u];
            }
            print_name[dir->name_len] = '\0';
            printf("%s\n", print_name);
            free(print_name);

            // Move to the next entry
            curr_pos = curr_pos + dir->rec_len;
            dir = (void*) dir + dir->rec_len;
        }
    }
}