#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <libgen.h>
#include "ext2.h"

extern int *find_bitmap(unsigned int map_loc, unsigned int count);
extern int find_dir_level(char *str);
extern int find_next_avaliable(int *map, int size, char c);
extern int new_dir_entry(struct ext2_dir_entry *parent_dir, struct ext2_dir_entry *prev_dir, int parent_b_num, char *directory_name);
extern void mark_use(int pos, unsigned int map, unsigned int count);
extern void set_inode(int i_num, unsigned int itable, unsigned int type, unsigned int b_num, unsigned int size);
extern int new_dir_init_second_level(int i_num, unsigned int parent_i_num);
extern int cal_rec_len(int name_length);

unsigned char *disk;
struct ext2_super_block *sb;
struct ext2_group_desc *bg;

int main(int argc, char *argv[]) {
    // usage check
    if (argc != 3 || argv[2][0] != '/') {
        fprintf(stderr, "usage: %s <image file name> <absolute path>\n", argv[0]);
        return EXIT_FAILURE;
    }
    // map disk image
    int fd = open(argv[1], O_RDWR);
    disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        return EXIT_FAILURE;
    }
    // super block
    sb = (struct ext2_super_block *)(disk + EXT2_BLOCK_SIZE);
    // block group
    bg = (struct ext2_group_desc *)(disk + EXT2_BLOCK_SIZE*2);
    struct ext2_inode *inode = (struct ext2_inode *)(disk + EXT2_BLOCK_SIZE * (bg->bg_inode_table));
    
    // find number of nested dir needed
    int num_nested = find_dir_level(argv[2]) - 1;
    
    // find desired location and/or check path validity
    // name of the directory we want to creat
    char *directory_name = basename(argv[2]);
    // names of each given directory level
    char *target_name = strtok(argv[2], "/");
    // some book keeping variables
    struct ext2_inode *curr_inode;
    struct ext2_dir_entry *curr_dir, *parent_dir, *prev_dir;
    int loop_count = 0, inode_num, block;
    // iterate each level of path
    while (target_name != NULL) {
        if (loop_count == 0) {
            // start with root directory
            inode_num = EXT2_ROOT_INO - 1;
        }
        curr_inode = inode + inode_num;
        int i;
        for (i = 0; i < (curr_inode->i_blocks)/2; i++) {
            block = curr_inode->i_block[i];
            curr_dir = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE * block);
            
            int total_len = 0, dir_exist = 0;
            while(total_len < EXT2_BLOCK_SIZE) {
                // check if desired name has been found
                if (strncmp(curr_dir->name, target_name, strlen(target_name)) != 0) {
                    total_len += curr_dir->rec_len;
                    prev_dir = curr_dir;
                    curr_dir = ((void *)curr_dir) + curr_dir->rec_len;
                    continue;
                }
                if (curr_dir->file_type != EXT2_FT_DIR) { // not a directory
                    return ENOTDIR;
                }
                // found next level directory
                dir_exist = 1;
                break;
            }
            if (!dir_exist)
                curr_dir = NULL;
        }
        if (curr_dir == NULL){ // did not find this directory
            // check if it is the dir want to make
            if (loop_count == num_nested) { // at bottom level
                strcpy(directory_name, target_name);
                break;
            } else { // not bottom level, the middle dirctory does not exist
                return ENOENT;
            }
        } else { // find this level, move to next level
            inode_num = (curr_dir->inode) - 1;
        }
        loop_count++;
        target_name = strtok(NULL, "/");
    }
    if (target_name == NULL) // directory already exists
        return EEXIST;
    else
        parent_dir = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE * block);
    
    int new_i_num = new_dir_entry(parent_dir, prev_dir, block, directory_name);
    if (new_i_num == ENOSPC)
        return ENOSPC;
    if (new_dir_init_second_level(new_i_num, parent_dir->inode) == ENOSPC)
        return ENOSPC;
    
    return 0;
}


// return a inode bitmap or block bitmap
int *find_bitmap(unsigned int map_loc, unsigned int count) {
    int *bitmap = malloc(sizeof(int) * count);
    unsigned char *bits = (unsigned char *)(disk + EXT2_BLOCK_SIZE * map_loc);
    int byte, bit, in_use;
    for (byte = 0; byte < count / 8; byte++) {
        for (bit = 0; bit < 8; bit++) {
            in_use = bits[byte] & (1 << bit);
            bitmap[byte * 8 + bit] = in_use >> bit;
        }
    }
    return bitmap;
}

// return number of nested directory level in a given path
int find_dir_level(char *str) {
    int i = 0;
    char *name = strtok(str, "/");
    while (name != NULL) {
        i++;
        name = strtok(NULL, "/");
    }
    return i;
}

// return next avaliable inode number or block number
int find_next_avaliable(int *map, int size, char c) {
    int p, lb = 0;
    if (c == 'i')
        lb = EXT2_GOOD_OLD_FIRST_INO - 1;
    for (p = size; p > lb + 1; p--) {
        if (p == 128) {
            p--;
        }
        if (map[p] == 0 && map[p-1] == 1)
            break;
    }
    if (size == sb->s_inodes_count) {
        sb->s_free_inodes_count = sb->s_free_inodes_count - 1;
        bg->bg_free_inodes_count = bg->bg_free_inodes_count - 1;
    } else {
        sb->s_free_blocks_count = sb->s_free_blocks_count - 1;
        bg->bg_free_blocks_count = bg->bg_free_blocks_count - 1;
    }
    return (p+1);
}

// construct a new directory entry and return its inode number
int new_dir_entry(struct ext2_dir_entry *parent_dir, struct ext2_dir_entry *prev_dir, int parent_b_num, char *directory_name) {
    unsigned int b_count = sb->s_blocks_count;
    unsigned int i_count = sb->s_inodes_count;
    unsigned int b_map = bg->bg_block_bitmap;
    unsigned int i_map = bg->bg_inode_bitmap;
    unsigned int itable = bg->bg_inode_table;
    // find block bitmap
    int *b_bitmap = find_bitmap(b_map, b_count);
    // find inode bitmap
    int *i_bitmap = find_bitmap(i_map, i_count);
    
    // find a next avaliable inode
    int inum = find_next_avaliable(i_bitmap, i_count, 'i');
    if (inum == i_count)
        return ENOSPC;
    mark_use(inum, i_map, i_count);
    
    int parent_i_num = parent_dir->inode;
    struct ext2_inode *inode = (struct ext2_inode *)(disk + EXT2_BLOCK_SIZE * itable);
    struct ext2_inode *parent_inode = inode + parent_i_num - 1;
    parent_inode->i_links_count = (parent_inode->i_links_count) + 1;
    // calculate new rec_len
    int new_rec_len = cal_rec_len(strlen(directory_name));
    int avaliable_rec_len = prev_dir->rec_len;
    int used_rec_len = cal_rec_len(prev_dir->name_len);
    // check if there are enough space for a new entry
    if ((avaliable_rec_len - used_rec_len) >= new_rec_len) {
        prev_dir->rec_len = used_rec_len;
        struct ext2_dir_entry *dir;
        dir = ((void *)prev_dir) + prev_dir->rec_len;
        dir->inode = inum;
        dir->rec_len = avaliable_rec_len - used_rec_len;
        dir->name_len = strlen(directory_name);
        dir->file_type = EXT2_FT_DIR;
        strcpy(dir->name, directory_name);
        // set new inode
        set_inode(inum, itable, EXT2_S_IFDIR, parent_b_num, 1024);
    } else {
        printf("shouldn'd be here\n");
        // allocate a new block for parent
        int bnum = find_next_avaliable(b_bitmap, b_count, 'b');
        if (bnum == b_count)
            return ENOSPC;
        mark_use(bnum, b_map, b_count);
        // link new block to parent directory
        parent_inode->i_blocks = (parent_inode->i_blocks) + 2;
        parent_inode->i_block[(parent_inode->i_blocks)/2] = bnum;
        // construct a new directory
        struct ext2_dir_entry *new_dir = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE * bnum);
        new_dir->inode = inum;
        new_dir->rec_len = 1024;
        new_dir->name_len = strlen(directory_name);
        new_dir->file_type = EXT2_FT_DIR;
        strcpy(new_dir->name, directory_name);
        // set new inode
        set_inode(inum, itable, EXT2_S_IFDIR, bnum, 1024);
        free(b_bitmap);
    }
    bg->bg_used_dirs_count = (bg->bg_used_dirs_count) + 1;
    free(i_bitmap);
    
    return inum;
}

// mark a inode or block used in bitmap
void mark_use(int pos, unsigned int map, unsigned int count) {
    unsigned char *bits = (unsigned char *)(disk + EXT2_BLOCK_SIZE * map);
    int byte, bit, i = 1;
    for (byte = 0; byte < count / 8; byte++) {
        for (bit = 0; bit < 8; bit++) {
            if (i == pos)
                *(bits + byte) = *(bits + byte) | (1 << bit);
            i++;
        }
    }
}

// set information for a inode
void set_inode(int i_num, unsigned int itable, unsigned int type, unsigned int b_num, unsigned int size) {
    struct ext2_inode *inode = (struct ext2_inode *)(disk + EXT2_BLOCK_SIZE * itable);
    inode = inode + i_num - 1;
    inode->i_mode = type;
    inode->i_uid = 0;
    inode->i_size = size;
    inode->i_gid = 0;
    inode->i_links_count = 2;
    inode->i_blocks = 2;
    inode->osd1 = 0;
    inode->i_generation = 0;
    inode->i_file_acl = 0;
    inode->i_dir_acl = 0;
    inode->i_faddr = 0;
}

// initialize the "." and ".." direcotry in the newly created directory
int new_dir_init_second_level(int i_num, unsigned int parent_i_num) {
    unsigned int b_count = sb->s_blocks_count;
    unsigned int b_map = bg->bg_block_bitmap;
    unsigned int itable = bg->bg_inode_table;
    int *b_bitmap = find_bitmap(b_map, b_count);
    
    // allocate a new block for the content
    int bnum = find_next_avaliable(b_bitmap, b_count, 'b');
    if (bnum == b_count)
        return ENOSPC;
    mark_use(bnum, b_map, b_count);
    struct ext2_inode *inode = (struct ext2_inode *)(disk + EXT2_BLOCK_SIZE * itable);
    inode = inode + i_num - 1;
    inode->i_block[0] = bnum;
    inode->i_block[1] = '\0';
    struct ext2_dir_entry *dir = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE * bnum);
    dir->inode = i_num;
    dir->rec_len = 12;
    dir->name_len = 1;
    dir->file_type = EXT2_FT_DIR;
    strcpy(dir->name, ".");
    
    dir = (void *)dir + 12;
    dir->inode = parent_i_num;
    dir->rec_len = EXT2_BLOCK_SIZE - 12;
    dir->name_len = 2;
    dir->file_type = EXT2_FT_DIR;
    strcpy(dir->name, "..");
    free(b_bitmap);
    return 0;
}

int cal_rec_len(int name_length) {
    int result;
    if (name_length % 4 == 0) {
        result = 8 + name_length;
    } else {
        result = 8 + 4 * (name_length / 4 + 1);
    }
    return result;
}

