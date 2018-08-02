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
// #include "helper.h"

#define SINGLE_INDIRECT 12

unsigned char *disk;
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
    return (struct ext2_super_block *)(disk + EXT2_BLOCK_SIZE);
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
struct ext2_inode *get_inode_table_loc(unsigned char *disk, struct ext2_group_desc *gd) {
    return (struct ext2_inode *)(disk + EXT2_BLOCK_SIZE * gd->bg_inode_table);
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
struct ext2_dir_entry_2 *get_dir_entry(unsigned char *disk, int block_num) {
    return (struct ext2_dir_entry_2 *) (disk + EXT2_BLOCK_SIZE * block_num);
}

/*
 * Return the inode of the root directory.
 */
struct ext2_inode *get_root_inode(struct ext2_inode  *inode_table) {
    return &(inode_table[EXT2_ROOT_INO - 1]);
}

/*
 * Return the file name of the given valid path.
 */
char *get_file_name(char *path) {
    char *filter = "/";
    char *file_name = malloc(sizeof(char) * (strlen(path) + 1));;

    char *full_path = malloc(sizeof(char) * (strlen(path) + 1));
    strncpy(full_path, path, strlen(path) + 1);

    char *token = strtok(full_path, filter);
    while (token != NULL) {
        strncpy(file_name, token, strlen(token) + 1);
        token = strtok(NULL, filter);
    }

    return file_name;
}

/*
 * Return the path except the final path name. With assumption that the path is
 * a valid path and it is a path of a file.
 *
 * If path is /a/bb/ccc/ this function will return /a/bb/ccc/ it will still
 * an invalid path.
 * If path is /a/bb/ccc function will give /a/bb/
 */
char *get_dir_path(char *path) {
    char *file_name = NULL;
    char *parent = NULL;
    file_name = strrchr(path, '/');
    parent = strndup(path, strlen(path) - strlen(file_name) + 1);
    return parent;
}

/*
 * Return the first inode number that is free.
 */
int get_free_inode(unsigned char *disk, unsigned char *inode_bitmap) {
    // Loop over the inodes that are not reserved, index i
    struct ext2_group_desc *gd = get_group_descriptor_loc(disk);
    struct ext2_super_block *sb = get_superblock_loc(disk);
    for (int i = EXT2_GOOD_OLD_FIRST_INO; i < sb->s_inodes_count; i++) {
        int bitmap_byte = i / 8;
        int bit_order = i % 8;
        if (!(1 & (inode_bitmap[bitmap_byte] >> bit_order))) {
            // Such bit is 0, which is a free inode
            if ((i + 1) % 8 == 0) {
                inode_bitmap[((i + 1) / 8) - 1] |= 1 << bit_order;
            } else {
                inode_bitmap[(i + 1) / 8] |= 1 << bit_order;
            }
            sb->s_free_inodes_count --;
            gd->bg_free_inodes_count --;
            return i + 1;
        }
    }

    return -1;
}

/*
 * Return the first block number that is free.
 */
int get_free_block(unsigned char *disk, unsigned char *block_bitmap) {
    // Loop over the inodes that are not reserved
    struct ext2_group_desc *gd = get_group_descriptor_loc(disk);
    struct ext2_super_block *sb = get_superblock_loc(disk);
    for (int i = 0; i < sb->s_blocks_count; i++) {
        int bitmap_byte = i / 8;
        int bit_order = i % 8;
        if (!(1 & (block_bitmap[bitmap_byte] >> bit_order))) {
            // Such bit is 0, which is a free block
            if ((i + 1) % 8 == 0) {
                block_bitmap[((i + 1) / 8) - 1] |= 1 << bit_order;
            } else {
                block_bitmap[(i + 1) / 8] |= 1 << bit_order;
            }
            sb->s_free_blocks_count --;
            gd->bg_free_blocks_count --;
            return i + 1;
        }
    }

    return -1;
}

/*
 * Return the inode of a directory/file/link with a particular name if it is in a block,
 * otherwise return NULL.
 */
struct ext2_inode *get_entry_in_block(unsigned char *disk, char *name, int block_num) {
    struct ext2_dir_entry_2 *dir = get_dir_entry(disk, block_num);
    struct ext2_inode *target = NULL;

    struct ext2_group_desc *gd = get_group_descriptor_loc(disk);
    struct ext2_inode  *inode_table = get_inode_table_loc(disk, gd);

    int curr_pos = 0; // used to keep track of the dir entry in each block
    while (curr_pos < EXT2_BLOCK_SIZE) {
        char *entry_name = malloc(sizeof(char) * dir->name_len + 1);

        for (int u = 0; u < dir->name_len; u++) {
            entry_name[u] = dir->name[u];
        }
        entry_name[dir->name_len] = '\0';

        if (strcmp(entry_name, name) == 0) {
            target = &(inode_table[dir->inode - 1]);
        }

        free(entry_name);

        /* Moving to the next directory */
        curr_pos = curr_pos + dir->rec_len;
        dir = (void*) dir + dir->rec_len;
    }

    return target;
}

/*
 * Return the inode of the directory/file/link with a particular name if it is in the given
 * parent directory, otherwise, return NULL.
 */
struct ext2_inode *get_entry_with_name(unsigned char *disk, char *name, struct ext2_inode *parent) {
    struct ext2_inode *target = NULL;

    // Search through the direct blocks
    for (int i = 0; i < SINGLE_INDIRECT; i++) {
        if (parent->i_block[i]) {
            target = get_entry_in_block(disk, name, parent->i_block[i]);
        }
    }

    // Search through the indirect blocks if not find the target inode
    if (target == NULL && parent->i_block[SINGLE_INDIRECT]) {
        unsigned int *indirect = get_indirect_block_loc(disk, parent);

        for (int i = 0; i < EXT2_BLOCK_SIZE / sizeof(unsigned int); i++) {
            if (indirect[i]) {
                target = get_entry_in_block(disk, name, indirect[i]);
            }
        }
    }

    return target;
}

/*
 * Trace the given path. Return the inode of the given path.
 */
struct ext2_inode *trace_path(char *path, unsigned char *disk) {
    char *filter = "/";

    struct ext2_group_desc *gd = get_group_descriptor_loc(disk);
    struct ext2_inode  *inode_table = get_inode_table_loc(disk, gd);

    // Get the inode of the root
    struct ext2_inode *current_inode = get_root_inode(inode_table);

    // Get the copy of the path
    char *full_path = malloc(sizeof(char) * (strlen(path) + 1));
    strncpy(full_path, path, strlen(path) + 1);

    char *token = strtok(full_path, filter);

    while (token != NULL) {
        if (current_inode != NULL && (current_inode->i_mode & EXT2_S_IFDIR)) {
            current_inode = get_entry_with_name(disk, token, current_inode);
        }

        token = strtok(NULL, filter);

        // handle case: in the middle of the path is not a directory
        if (current_inode != NULL && token != NULL
            && !(current_inode->i_mode & EXT2_S_IFDIR)) {
            return NULL;
        }
    }

    // handle the case like: /a/bb/ccc/, ccc need to be a directory
    if (current_inode != NULL
        && (path[strlen(path) - 1] == '/')
        && !(current_inode->i_mode & EXT2_S_IFDIR)) {
        return NULL;
    }

    return current_inode;
}

/*
 * Add new entry into the directory.
 */
int add_new_entry(unsigned char *disk, struct ext2_inode *dir_inode, unsigned int new_inode, char *f_name, char type) {
    // Recalls that there are 12 direct blocks.
    struct ext2_group_desc *gd = get_group_descriptor_loc(disk);

    unsigned char *b_bitmap =  disk + EXT2_BLOCK_SIZE * (gd->bg_block_bitmap);
    int block_num;
    int length = (int)(strlen(f_name) + sizeof(struct ext2_dir_entry_2 *));
    // int length = 1021;
    struct ext2_dir_entry_2 *dir = NULL;
    for (int k = 0; k < 12; k++) {
        // If the block does not exist yet i.e. block number = 0
        if ((block_num = dir_inode->i_block[k]) == 0) {
            int free_block_num = get_free_block(disk, b_bitmap);
            if (free_block_num == -1) { // No extra free blocks for new entry
                return -1;
            }
            dir_inode->i_block[k] = (unsigned int)free_block_num;
            dir_inode->i_blocks += 2;
            dir_inode->i_size += EXT2_BLOCK_SIZE;
            dir = get_dir_entry(disk, free_block_num);
            length = EXT2_BLOCK_SIZE;
            break;
        }

        dir = get_dir_entry(disk, block_num);
        int curr_pos = 0;

        /* Total size of the directories in a block cannot exceed a block size */
        while (curr_pos < EXT2_BLOCK_SIZE) {
            int true_len = sizeof(struct ext2_dir_entry_2 *) + dir->name_len;
            while (true_len % 4 != 0) {
                true_len ++;
            }
            if ((dir->rec_len - true_len) >= length) {
                int orig_rec_len = dir->rec_len;
                dir->rec_len = (unsigned char) true_len;
                dir = (void *) dir + true_len;
                length = orig_rec_len - true_len;
                k = 12; // also terminate the for loop
                break;
            }
            // Moving to the next directory
            curr_pos = curr_pos + dir->rec_len;
            dir = (void *) dir + dir->rec_len;
        }
    }
    if (dir == NULL) {
        return -1;
    }
    dir->inode = new_inode;
    dir->name_len = (unsigned char) strlen(f_name);
    memcpy(dir->name, f_name, dir->name_len);
    if (type == 'd') {
        dir->file_type = EXT2_FT_DIR;
        dir_inode->i_links_count ++;
    } else if (type == 'f') {
        dir->file_type = EXT2_FT_REG_FILE;
    } else if (type == 'l') {
        dir->file_type = EXT2_FT_SYMLINK;
    } else {
        dir->file_type = EXT2_FT_UNKNOWN;
    }
    dir->rec_len = (unsigned short)length;
    return 0;
}

/*
 * Get inode number of given inode if exist, otherwise return 0.
 */
int get_inode_num(unsigned char *disk, struct ext2_inode *target) {
    struct ext2_super_block *sb = get_superblock_loc(disk);
    struct ext2_group_desc *gd = get_group_descriptor_loc(disk);
    struct ext2_inode  *inode_table = get_inode_table_loc(disk, gd);

    int inode_num = 0;

    for (int i = 0; i < sb->s_inodes_count; i++) {
        if ((inode_table[i].i_size != 0) && (&(inode_table[i]) == target)) { // there is data
            inode_num = i + 1;
            return inode_num;
        }
    }

    return inode_num;
}

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
