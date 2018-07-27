#include <stdio.h>
#include <stdlib.h>

#include "ext2.h"

unsigned char *get_disk_loc(char *disk_name);
struct ext2_super_block *get_superblock_loc(unsigned char *disk);
struct ext2_group_desc *get_group_descriptor_loc(unsigned char *disk);
unsigned char *get_block_bitmap_loc(unsigned char *disk, struct ext2_group_desc *gd);
unsigned char *get_inode_bitmap_loc(unsigned char *disk, struct ext2_group_desc *gd);
struct ext2_inode *get_inode_table_loc(unsigned char *disk, struct ext2_group_desc *gd);
unsigned int *get_indirect_block_loc(unsigned char *disk, struct ext2_inode  *inode);

struct ext2_dir_entry_2 *get_dir_entry(unsigned char *disk, int block_num);
struct ext2_inode *get_root_inode(struct ext2_inode  *inode_table);
char *get_file_name(char *path);

struct ext2_inode *trace_path(char *path, unsigned char *disk);
struct ext2_inode *get_entry_with_name(unsigned char *disk, char *name, struct ext2_inode *parent);
struct ext2_inode *get_entry_in_block(unsigned char *disk, char *name, int block_num);

void print_entries(unsigned char *disk, struct ext2_inode *dir, char *flag);
void print_one_block_entries(struct ext2_dir_entry_2 *dir, char *flag);
