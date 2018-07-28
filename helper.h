#include <stdio.h>
#include <stdlib.h>

#include "ext2.h"

#define SINGLE_INDIRECT 12

/*
 * Return the disk location.
 */
unsigned char *get_disk_loc(char *disk_name);

/*
 * Return the super block location.
 */
struct ext2_super_block *get_superblock_loc(unsigned char *disk);

/*
 * Return the group descriptor location.
 */
struct ext2_group_desc *get_group_descriptor_loc(unsigned char *disk);

/*
 * Return the block bitmap (16*8 bits) location.
 */
unsigned char *get_block_bitmap_loc(unsigned char *disk, struct ext2_group_desc *gd);

/*
 * Return the inode bitmap (4*8 bits) location.
 */
unsigned char *get_inode_bitmap_loc(unsigned char *disk, struct ext2_group_desc *gd);

/*
 * Return the inode table location.
 */
struct ext2_inode *get_inode_table_loc(unsigned char *disk, struct ext2_group_desc *gd);

/*
 * Return the indirect block location.
 */
unsigned int *get_indirect_block_loc(unsigned char *disk, struct ext2_inode  *inode);

/*
 * Return the directory location.
 */
struct ext2_dir_entry_2 *get_dir_entry(unsigned char *disk, int block_num);

/*
 * Return the inode of the root directory.
 */
struct ext2_inode *get_root_inode(struct ext2_inode  *inode_table);

/*
 * Return the file name of the given valid path.
 */
char *get_file_name(char *path);

/*
 * Return the path except the final path name. With assumption that the path is
 * a valid path and it is a path of a file.
 */
char *get_dir_path(char *path);

/*
 * Return the first inode number that is free.
 */
int get_free_inode(struct ext2_super_block *sb, unsigned char *inode_bitmap);

/*
 * Return the first block number that is free.
 */
int get_free_block(struct ext2_super_block *sb, unsigned char *block_bitmap);

/*
 * Trace the given path. Return the inode of the given path.
 */
struct ext2_inode *trace_path(char *path, unsigned char *disk);

/*
 * Return the inode of the directory/file/link with a particular name if it is in the given
 * parent directory, otherwise, return NULL.
 */
struct ext2_inode *get_entry_with_name(unsigned char *disk, char *name, struct ext2_inode *parent);

/*
 * Return the inode of a directory/file/link with a particular name if it is in a block,
 * otherwise return NULL.
 */
struct ext2_inode *get_entry_in_block(unsigned char *disk, char *name, int block_num);

/*
 * Print all the entries of a given directory.
 */
void print_entries(unsigned char *disk, struct ext2_inode *dir, char *flag);

/*
 * Print the entries in one block
 */
void print_one_block_entries(struct ext2_dir_entry_2 *dir, char *flag);

/*
 * Create a new directory entry for the new enter file, link or directory.
 */
struct ext2_dir_entry_2 *setup_entry(unsigned int new_inode, char *f_name, char type);

/*
 * Add new entry into the directory.
 */
int add_new_entry(unsigned char *disk, struct ext2_inode *dir_inode, struct ext2_dir_entry_2 *new_entry);

/*
 * Zero the block bitmap of given inode.
 */
void zero_block_bitmap(unsigned char *disk, struct ext2_inode *remove);

/*
 * Zero the given inode from the inode bitmap
 */
void zero_inode_bitmap(unsigned char *disk, struct ext2_inode *remove);
