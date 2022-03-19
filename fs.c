#include "fs.h"
#include "disk.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#define FS_MAGIC           0xf0f03410
#define INODES_PER_BLOCK   128
#define POINTERS_PER_INODE 5
#define POINTERS_PER_BLOCK 1024

struct fs_superblock {
	int magic;
	int nblocks;
	int ninodeblocks;
	int ninodes;
};

struct fs_inode {
	int isvalid;
	int size;
	int direct[POINTERS_PER_INODE];
	int indirect;
};

union fs_block {
	struct fs_superblock super;
	struct fs_inode inode[INODES_PER_BLOCK];
	int pointers[POINTERS_PER_BLOCK];
	char data[DISK_BLOCK_SIZE];
};

static int *bitmap;
static int  is_mounted;

int minimum(int a, int b) {
	return a<b?a:b;
}

int fs_format()
{
	if(is_mounted == 1) {
		return 0;
	}
	int disk_blocks         = disk_size();
	union fs_block block;
	int cur_disk_block       = 0;
	memset(block.data, 0, sizeof(block));
	block.super.magic        = FS_MAGIC;
	block.super.nblocks      = disk_blocks;
	block.super.ninodeblocks = disk_blocks*0.1+1;
	block.super.ninodes      = block.super.ninodeblocks*INODES_PER_BLOCK;
	disk_write(0, block.data);     //super block written
	cur_disk_block++;

	//Making isvalid flag 0 for all the inodes
	int num_inode_blocks     = block.super.ninodeblocks;
	memset(block.data, 0, sizeof(block));
	
	for(int i=0;i<num_inode_blocks;i++) {
		disk_write(cur_disk_block, block.data);
		cur_disk_block++;
	}

	return 1;

}

void fs_debug()
{
	if(is_mounted == 0) {
		printf("File system not mounted\n");
		return;
	}
	union fs_block block;

	disk_read(0,block.data);

	printf("superblock:\n");
	if(block.super.magic == 0xf0f03410) {
		printf("    magic number is valid\n");
	} else {
		printf("    magic number is invalid.Disk corrupted.Abort\n");
		return;
	}
	printf("    %d blocks on disk\n",block.super.nblocks);
	printf("    %d inode blocks for inodes\n",block.super.ninodeblocks);
	printf("    %d inodes total\n",block.super.ninodes);

	
	int num_inode_blocks = block.super.ninodeblocks;
	int cur_inode        = 0;  
	for(int bl=1; bl<= num_inode_blocks; bl++) {
		disk_read(bl, block.data);
		for(int i=0;i<INODES_PER_BLOCK;i++) {
			if(block.inode[i].isvalid==1) {
				struct fs_inode inode = block.inode[i];
				printf("inode %d\n", cur_inode);
				printf("    %d size\n", inode.size);
				if(inode.size>0) {
					printf("    direct blocks: ");
					for(int i=0; i<POINTERS_PER_INODE; i++) {
						if(inode.direct[i]!= 0) {
							printf("%d ", inode.direct[i]);
						}
					}
					printf("\n");

					if(inode.indirect!=0) {
						int indirect_block = inode.indirect;
						printf("    indirect block: %d\n", indirect_block);
						disk_read(indirect_block, block.data);
						printf("    indirect data blocks: ");
						for(int j=0;j<POINTERS_PER_BLOCK;j++) {
							if(block.pointers[j]!=0) {
								printf("%d ",block.pointers[j]);
							}
						}
						printf("\n");
						disk_read(bl, block.data);
					}
		        }

			}
			cur_inode++;
		}
	}
}
	

int fs_mount()
{
	union fs_block block;

	disk_read(0,block.data);

	if(block.super.magic != 0xf0f03410) {
		return 0;                                   //valid file system not present
	}
	bitmap = (int*)malloc(disk_size()*sizeof(int)); //initializing bitmap
    if(bitmap == NULL) {
    	return 0;                                  //could not allocate memory for bitmap
    }
    memset(bitmap,0,disk_size());
    bitmap[0] = 1;                                 //disk 0 superblock is always allocated

	int num_inode_blocks = block.super.ninodeblocks;

	for(int bl=1; bl<= num_inode_blocks; bl++) {
		disk_read(bl, block.data);
		bitmap[bl] = 1;
		for(int i=0;i<INODES_PER_BLOCK;i++) {
			if(block.inode[i].isvalid==1) {
				struct fs_inode inode = block.inode[i];
				if(inode.size>0) {
					for(int i=0; i<POINTERS_PER_INODE; i++) {
						if(inode.direct[i]!= 0) {
							bitmap[inode.direct[i]]=1;
						}
					}
					
					if(inode.indirect!=0) {
						int indirect_block = inode.indirect;
						bitmap[indirect_block]=1;
						disk_read(indirect_block, block.data);
						for(int j=0;j<POINTERS_PER_BLOCK;j++) {
							if(block.pointers[j]!=0) {
								bitmap[block.pointers[j]]=1;
							}
						}
						disk_read(bl, block.data);
					}
		        }
			}
		}
	}

	
	
    is_mounted = 1;
	return 1;
}

int fs_create()
{
	if(is_mounted == 0) {
		printf("File system not mounted\n");
		return -1;
	}
	union fs_block block;
	int cur_disk_block = 0;
	disk_read(0,block.data);
	cur_disk_block++;

	int num_inode_blocks = block.super.ninodeblocks;
	
	int inode_idx = 0;

	for(int i=1; i<=num_inode_blocks; i++) {
		disk_read(i, block.data);
		for(int j=0;j<INODES_PER_BLOCK; j++) {
			if(block.inode[j].isvalid == 0) {
				block.inode[j].isvalid=1;
				block.inode[j].size=0;
				for(int k=0;k<POINTERS_PER_INODE;k++) {
					block.inode[j].direct[k]=0;
				}
				block.inode[j].indirect=0;
				disk_write(i, block.data);
				return inode_idx;
			}
			inode_idx++;
		}
	}

	return -1;   //inode table full
}

int fs_delete( int inumber )
{
	union fs_block block;
	disk_read(0, block.data);
	if((inumber<0) || (inumber>=block.super.ninodes)||(is_mounted==0)) {  //invalid inumber input
		return 0;
	}
	int inode_block_num = inumber/INODES_PER_BLOCK+1;
	int inode_block_idx = inumber%INODES_PER_BLOCK;

	disk_read(inode_block_num, block.data);
	if(block.inode[inode_block_idx].isvalid == 0) {  //inode already invalid(free)
		return 0;
	}

	block.inode[inode_block_idx].isvalid = 0;
	for(int i=0; i<POINTERS_PER_INODE; i++) {
		if(block.inode[inode_block_idx].direct[i]!=0) {
			bitmap[block.inode[inode_block_idx].direct[i]] = 0; //freeing direct blocks
			block.inode[inode_block_idx].direct[i]         = 0;
		}
	}

	int indirect_block = block.inode[inode_block_idx].indirect;
	block.inode[inode_block_idx].size = 0;
	disk_write(inode_block_num, block.data);
	
	if(indirect_block != 0) {
		disk_read(indirect_block, block.data);
		for(int i=0;i<POINTERS_PER_BLOCK;i++) {
			if(block.pointers[i]!=0) {                  //freeing indirect blocks
				bitmap[block.pointers[i]] = 0;
				block.pointers[i]         = 0;
			}
		}

		disk_write(indirect_block, block.data);
	}


	return 1;
}

int fs_getsize( int inumber )
{
	union fs_block block;
	disk_read(0, block.data);
	if((inumber<0) || (inumber>=block.super.ninodes)||(is_mounted==0)) {  //invalid inumber input
		return -1;
	}
	int inode_block_num = inumber/INODES_PER_BLOCK+1;
	int inode_block_idx = inumber%INODES_PER_BLOCK;

	disk_read(inode_block_num, block.data);
	if(block.inode[inode_block_idx].isvalid == 0) {  //inode is free
		return -1;
	}

	return block.inode[inode_block_idx].size;
}

int fs_read( int inumber, char *data, int length, int offset )
{
	memset(data,0,length*sizeof(data[0]));
	union fs_block block;
	disk_read(0, block.data);
	if((inumber<0) || (inumber>=block.super.ninodes)||(length==0)||(is_mounted==0)) {  //invalid inumber input
		return 0;
	}

	int inode_block_num = inumber/INODES_PER_BLOCK+1;
	int inode_block_idx = inumber%INODES_PER_BLOCK;

	disk_read(inode_block_num, block.data);
	if((block.inode[inode_block_idx].isvalid == 0)||(offset>=block.inode[inode_block_idx].size)) {  
		return 0;
	}

	struct fs_inode inode = block.inode[inode_block_idx];

	//Assuming that indirect disk blocks are filled only after all the direct disk blocks are filled
	int begin_disk_num        = offset/DISK_BLOCK_SIZE;
	int last_byte             = minimum(offset+length-1, inode.size-1);
	int end_disk_num          = last_byte/DISK_BLOCK_SIZE;
	int bytes_copied          = minimum(length, (inode.size-offset));

	
	union fs_block indirect_block;

	if(end_disk_num>=POINTERS_PER_INODE) {
		printf("Reading indirect block\n");
		disk_read(inode.indirect, indirect_block.data);
	}

	for(int i=begin_disk_num;i<=end_disk_num;i++) {
		if(i >= POINTERS_PER_INODE) {
            disk_read(indirect_block.pointers[i-POINTERS_PER_INODE], block.data);
		} else {
			disk_read(inode.direct[i], block.data);
		}
		if(i==begin_disk_num) {
			int strt_byte = offset%DISK_BLOCK_SIZE;
			if(i==end_disk_num) {
				char str[DISK_BLOCK_SIZE];
				memset(str,0,sizeof(str));
				int end_byte = (strt_byte+bytes_copied-1)%DISK_BLOCK_SIZE;
				int cur = 0;
				for(int i=strt_byte;i<=end_byte;i++) {
					str[cur++] = block.data[i];
				}
                strcat(data, str);
			} else {
				char* str;
				str = block.data+ strt_byte;
				strcat(data, str);
		    }
			
		} else if(i==end_disk_num) {
			int strt_byte = offset%DISK_BLOCK_SIZE;
			char str[DISK_BLOCK_SIZE];
			memset(str,0,DISK_BLOCK_SIZE*sizeof(str[0]));
			int end_byte = (bytes_copied-(DISK_BLOCK_SIZE-strt_byte)-1)%DISK_BLOCK_SIZE;
			for(int j=0;j<=end_byte;j++) {
				str[j] = block.data[j];
			}
			strcat(data, str);


		} else {
			strcat(data, block.data);
		}
	}

	return bytes_copied;
}

void bitmap_status() {
	for(int i=0;i<disk_size();i++){
		printf("%d ",bitmap[i]);
	}
	printf("\n\n");
}

int get_free_block(int num_inode_blocks) {
	bitmap_status();
	int begin_block_search = 1+num_inode_blocks+1;
	for(int i=begin_block_search;i<disk_size();i++) {
		if(bitmap[i]==0){
			bitmap[i] = 1;
			return i;
		}
	}
	return -1;
}


int fs_write( int inumber, const char *data, int length, int offset )
{
	
	union fs_block block;
	disk_read(0, block.data);
	if((inumber<0) || (inumber>=block.super.ninodes)||(is_mounted==0)) {  //invalid inumber input
		return 0;
	}
	int num_inode_blocks = block.super.ninodeblocks; 
	int inode_block_num  = inumber/INODES_PER_BLOCK+1;
	int inode_block_idx  = inumber%INODES_PER_BLOCK;

	disk_read(inode_block_num, block.data);

	union  fs_block inode_block = block;
	
	inode_block.inode[inode_block_idx].isvalid = 1;
	int strt_disk_num         = offset/DISK_BLOCK_SIZE;
	int last_byte             = offset+length-1;
	int end_disk_num          = last_byte/DISK_BLOCK_SIZE;
	int bytes_copied          = 0;


    int cur_idx = 0;

	for(int i=strt_disk_num;i<=end_disk_num;i++) {
		bitmap_status();
		int free_block = get_free_block(num_inode_blocks);
		if(free_block == -1) {
			inode_block.inode[inode_block_idx].size+=bytes_copied;
			disk_write(inode_block_num, inode_block.data);
			return bytes_copied;
		}
		char cur_data[DISK_BLOCK_SIZE];
		int strt_cp_byte = cur_idx*DISK_BLOCK_SIZE;
		int end_cp_byte  = minimum((cur_idx+1)*DISK_BLOCK_SIZE-1, length-1);
		int idx = 0;
		for(int j=strt_cp_byte; j<=end_cp_byte;j++) {    //copying data to disk
			cur_data[idx] = data[j];
			idx++;
		}
		
		if(i<POINTERS_PER_INODE) {                       //direct pointers
			inode_block.inode[inode_block_idx].direct[i] = free_block;
		} else {
			if(inode_block.inode[inode_block_idx].indirect==0) {
				int indirect_free_ptr_block = get_free_block(num_inode_blocks);
				if(indirect_free_ptr_block == -1) {   //free block for indirect block pointers not available
					inode_block.inode[inode_block_idx].size+=bytes_copied;
					disk_write(inode_block_num, inode_block.data);
					bitmap[free_block] = 0;
					return bytes_copied;
				} else {                  //free block for indirect block pointers not available
					inode_block.inode[inode_block_idx].indirect = indirect_free_ptr_block;
					union fs_block indirect_block;
					memset(indirect_block.pointers,0, POINTERS_PER_BLOCK*sizeof(indirect_block.pointers[0]));
					indirect_block.pointers[0] = free_block;
					disk_write(indirect_free_ptr_block, indirect_block.data);
				}
			} else {                         //already indirect block pointers allocated
				union fs_block indirect_block;
				disk_read(inode_block.inode[inode_block_idx].indirect, indirect_block.data);
				int j;
				for(j=0;j<POINTERS_PER_BLOCK;j++) {
					if(indirect_block.pointers[j]==0) {
						break;
					}
				}
				if(j==POINTERS_PER_BLOCK) { //maximum file size exceeded
					inode_block.inode[inode_block_idx].size+=bytes_copied;
					disk_write(inode_block_num, inode_block.data);
					return bytes_copied;
				} else {
					indirect_block.pointers[j] = free_block;
					disk_write(inode_block.inode[inode_block_idx].indirect, indirect_block.data);
				}
			}
		}

		disk_write(free_block, cur_data);
		cur_idx++;
		bytes_copied += (end_cp_byte-strt_cp_byte+1);
	}
	inode_block.inode[inode_block_idx].size+=length;
 	disk_write(inode_block_num, inode_block.data);
 	fs_debug();
	return length;
}