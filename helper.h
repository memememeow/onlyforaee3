#include <stdio.h>
#include <stdlib.h>

#include "ext2.h"

unsigned char *get_disk_loc(char *disk_name);
struct ext2_super_block *get_superblock_loc(unsigned char *disk);
struct ext2_group_desc *get_group_descriptor_loc(unsigned char *disk);
unsigned char *get_block_bitmap_loc(unsigned char *disk, struct ext2_group_desc *gd);
unsigned char *get_inode_bitmap_loc(unsigned char *disk, struct ext2_group_desc *gd);
struct ext2_inode  *get_inode_table_loc(unsigned char *disk, struct ext2_group_desc *gd);
unsigned int *get_indirect_block_loc(unsigned char *disk, struct ext2_inode  *inode);
struct ext2_dir_entry_2 *get_directory_loc(unsigned char *disk, struct ext2_inode  *inode, int i_block);
struct ext2_inode *get_root_inode(struct ext2_inode  *inode_table);
struct ext2_inode *trace_path(char *path, unsigned char *disk);
char *get_file_name(char *path);
