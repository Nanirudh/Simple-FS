/*
Code template taken from https://www-users.cselabs.umn.edu/classes/Fall-2019/csci5103/tmp/project3/project3.html
*/

#include "fs.h"
#include "disk.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#define FS_MAGIC           0xf0f03410
#define INODES_PER_BLOCK   128
#define POINTERS_PER_INODE 5
#define POINTERS_PER_BLOCK 1024
#define DIR_PATH_SIZE      20
#define FILE_NAME_SIZE     24
#define DIR_ENTRIES_PER_BLOCK 128

struct fs_superblock {
	int magic;
	int nblocks;
	int ninodeblocks;
	int ninodes;
};

union attributes {
	int direct[POINTERS_PER_INODE];
	char dir_name[DIR_PATH_SIZE];
};

struct fs_inode {
	int isvalid;
	int size;
	union attributes attr;
	int indirect;
};

struct dir_block {
	char name[FILE_NAME_SIZE];
	int type;
	int inode_num;
};

union fs_block {
	struct fs_superblock super;
	struct fs_inode inode[INODES_PER_BLOCK];
	int pointers[POINTERS_PER_BLOCK];
	char data[DISK_BLOCK_SIZE];
	struct dir_block dir[DIR_ENTRIES_PER_BLOCK];
};

static int *bitmap;
static int  is_mounted;

int minimum(int a, int b) {
	return a<b?a:b;
}

int fs_format()
{
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

	//creating root directory in the file system after formatting
	disk_read(1, block.data);
	block.inode[0].isvalid = 2;
	block.inode[0].indirect = 1+num_inode_blocks;
	block.inode[0].size = 0;
	strcpy( block.inode[0].attr.dir_name, "root");
	disk_write(1,block.data);

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
			if(block.inode[i].isvalid==1) { //file

				struct fs_inode inode = block.inode[i];
				printf("inode %d\n", cur_inode);
				printf("    %d size\n", inode.size);
				if(inode.size>0) {
					printf("    direct blocks: ");
					for(int i=0; i<POINTERS_PER_INODE; i++) {
						if(inode.attr.direct[i]!= 0) {
							printf("%d ", inode.attr.direct[i]);
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

			} else if(block.inode[i].isvalid==2) {   //directory
				struct fs_inode inode = block.inode[i];
				printf("inode %d\n", cur_inode);
				printf("    %d size\n", inode.size);
				printf("    directory name %s\n", inode.attr.dir_name);
				printf("    directory block %d\n", inode.indirect);

				if(inode.size>0) {
					printf("    directory contents:\n");
					int indirect_block = inode.indirect;
					int sz             = inode.size;
					disk_read(indirect_block, block.data);
					for(int j=0;j<sz;j++) {
						if(block.dir[j].type==1) {
							printf("    directory name: ");
						} else {
							printf("    file name: ");
						}
						printf("%s\t",block.dir[j].name);
						printf("inode: %d\n", block.dir[j].inode_num);
					}
					disk_read(bl, block.data);
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
						if(inode.attr.direct[i]!= 0) {
							bitmap[inode.attr.direct[i]]=1; //updating bitmap with occupied disk data
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
			} else if(block.inode[i].isvalid==2) {
				struct fs_inode inode = block.inode[i];
				bitmap[inode.indirect]=1;
			}
		}
	}

	
	
    is_mounted = 1;
	return 1;
}

//creates a file.  parent directory inode no and file name should be provided
int fs_create(int dir_inode_no, char* file_name)
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
    int file_inode_block = 0;
	for(int i=1; i<=num_inode_blocks; i++) {
		disk_read(i, block.data);
		for(int j=0;j<INODES_PER_BLOCK; j++) {
			if(block.inode[j].isvalid == 0) {
				block.inode[j].isvalid=1;
				block.inode[j].size=0;
				for(int k=0;k<POINTERS_PER_INODE;k++) {
					block.inode[j].attr.direct[k]=0;
				}
				block.inode[j].indirect=0;
				//disk_write(i, block.data);
				file_inode_block = i;
				break;
			}
			
			inode_idx++;
		}
		if(file_inode_block>0) {
				break;
			}
	}
	if(file_inode_block==0)
		return -1;   //inode table full

	union fs_block dir_block;
	union fs_block dir_entry_block;
	int dir_inode_block     = dir_inode_no/INODES_PER_BLOCK + 1;//first block is superblock
	int dir_inode_block_idx = dir_inode_no % INODES_PER_BLOCK;
	disk_read(dir_inode_block, dir_block.data);
	if((dir_block.inode[dir_inode_block_idx].isvalid!=2) || (dir_block.inode[dir_inode_block_idx].size == DIR_ENTRIES_PER_BLOCK)) {
		return -1; //Not a directory or directory full
	} 

	int dir_entry_block_no = dir_block.inode[dir_inode_block_idx].indirect;
	int dir_size           = dir_block.inode[dir_inode_block_idx].size;

	//checking if duplicate filenames are present
	disk_read(dir_entry_block_no, dir_entry_block.data);
	for(int i=0;i<dir_size; i++) {
		if(strcmp(dir_entry_block.dir[i].name, file_name)==0) {
			printf("File already exists in the directory\n");
			return -1;
		}
	}

	strcpy(dir_entry_block.dir[dir_size].name, file_name);
	dir_entry_block.dir[dir_size].inode_num = inode_idx;
	dir_entry_block.dir[dir_size].type = 0;
	disk_write(dir_entry_block_no, dir_entry_block.data);


	dir_block.inode[dir_inode_block_idx].size++;

	//printf("writing to %d %d %d %d\n",dir_inode_block,dir_inode_block_idx, dir_entry_block_no, dir_size);
	if(file_inode_block != dir_inode_block) {
		disk_write(file_inode_block, block.data);
		
	} else {
		dir_block.inode[inode_idx%INODES_PER_BLOCK].isvalid = 1;
		dir_block.inode[inode_idx%INODES_PER_BLOCK].size = 0;
		for(int k=0;k<POINTERS_PER_INODE;k++) {
			dir_block.inode[inode_idx%INODES_PER_BLOCK].attr.direct[k]=0;
		}
		dir_block.inode[inode_idx%INODES_PER_BLOCK].indirect=0;
	}
	disk_write(dir_inode_block, dir_block.data);

	return inode_idx;

}

//inode no of file and inode no of parent directory
int fs_delete( int inumber, int dir_inode_no)
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
		if(block.inode[inode_block_idx].attr.direct[i]!=0) {
			bitmap[block.inode[inode_block_idx].attr.direct[i]] = 0; //freeing direct blocks
			block.inode[inode_block_idx].attr.direct[i]         = 0;
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

	//removing entry from directory
	int dir_inode_block_idx = dir_inode_no % INODES_PER_BLOCK;
	int dir_block_idx       = dir_inode_no/INODES_PER_BLOCK+1;
	

	disk_read(dir_block_idx, block.data);
	int dir_entry_block_idx = block.inode[dir_inode_block_idx].indirect;
	int dir_entry_sz        = block.inode[dir_inode_block_idx].size;
	block.inode[dir_inode_block_idx].size--;
	disk_write(dir_block_idx, block.data);

	if(dir_entry_sz>1) {
		disk_read(dir_entry_block_idx, block.data);

		for(int i=0;i<dir_entry_sz; i++) {
			if(block.dir[i].inode_num==inumber) {
				for(int j=i+1;j<dir_entry_sz;j++) {
					block.dir[j-1]=block.dir[j];
				}
				break;
			}
		}
		disk_read(dir_entry_block_idx, block.data);
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
		disk_read(inode.indirect, indirect_block.data);
	}

	for(int i=begin_disk_num;i<=end_disk_num;i++) {
		if(i >= POINTERS_PER_INODE) {
            disk_read(indirect_block.pointers[i-POINTERS_PER_INODE], block.data);
		} else {
			disk_read(inode.attr.direct[i], block.data);
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

//helper fn
void bitmap_status() {
	for(int i=0;i<disk_size();i++){
		printf("%d ",bitmap[i]);
	}
	printf("\n\n");
}

//helper fn
int get_free_block(int num_inode_blocks) {
	//bitmap_status();
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
		//bitmap_status();
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
			inode_block.inode[inode_block_idx].attr.direct[i] = free_block;
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

//helper fn
//returns inode no of the directory
struct fs_inode get_dir_inode(char* dir_name, int num_inode_blocks) {
	
	union fs_block block;

	for(int bl=1; bl<= num_inode_blocks; bl++) {
		disk_read(bl, block.data);
		for(int i=0;i<INODES_PER_BLOCK;i++) {
			if(block.inode[i].isvalid==2 && strcmp(block.inode[i].attr.dir_name,dir_name)==0) {
				return block.inode[i];
			}
			
		}
	}
	
	return block.inode[0];
}

//helper fn
//checks if the path provided is valid
int is_valid_path(char* dir_path) {

	int num_delimit = 0;
	const char delimit[2] = "/";
	char* token;

	for(int i=0;i<strlen(dir_path);i++) {
		if(dir_path[i]=='/' && i<strlen(dir_path)-1) {
			num_delimit++;
		}
	}

	if(dir_path[0]!='/') {
		num_delimit++;
	}


	union fs_block block;
	//int inode_num = 1;
	int block_num,sz;

	disk_read(0,block.data);
	int num_inode_blocks = block.super.ninodeblocks;

	disk_read(1, block.data);
	struct fs_inode inode_val = block.inode[0];
	

	for(int i=0;i<num_delimit-1;i++) {
		
		if(i==0) {
			token = strtok(dir_path, delimit);
	    } else {
	    	token = strtok(NULL, delimit);
	    }
	    block_num = inode_val.indirect;
	    sz        = inode_val.size;
	    disk_read(block_num,block.data);
	    int flag = 0;
	    for(int j=0;j<sz;j++) {
	    	if(strcmp(token, block.dir[j].name)==0) {
	    		flag=1;
	    		break;
	    	}
	    }
	    if(flag==0) {
	    	return 0;
	    }
	    inode_val = get_dir_inode(token, num_inode_blocks);

	}

	block_num = inode_val.indirect;
    sz        = inode_val.size;
  
    disk_read(block_num,block.data);


    if(num_delimit==1) {
    	token = strtok(dir_path, delimit);
    } else {
    	token = strtok(NULL, delimit);
    }
    
    for(int j=0;j<sz;j++) {
    	if(strcmp(token, block.dir[j].name)==0) {
    		return 0;
    	}
    }

    return 1;


}

//returns first vacant inode
int fs_get_vacant_inode(char* dir_name, int num_inode_blocks)
{
	
	union fs_block block;
	//int cur_disk_block = 0;
	
	
	int inode_idx = 0;

	for(int i=1; i<=num_inode_blocks; i++) {
		disk_read(i, block.data);
		for(int j=0;j<INODES_PER_BLOCK; j++) {
			if(block.inode[j].isvalid == 0) {
				block.inode[j].isvalid=2;
				block.inode[j].size=0;
				
				int blk = get_free_block(num_inode_blocks);
				if(blk==-1) {
					return -1;
				}
				block.inode[j].indirect = blk;
				strcpy(block.inode[j].attr.dir_name,dir_name);
				disk_write(i, block.data);
				return inode_idx;
			}
			inode_idx++;
		}
	}

	return -1;   //inode table full
}


int fs_create_dir(char* dir_path) {
	if(is_mounted == 0) {
		printf("File system not mounted\n");
		return -1;
	}
	
	char dir_path_cp[DIR_PATH_SIZE];
	strcpy(dir_path_cp, dir_path);
	//checks if the given directory path is valid
	if(is_valid_path(dir_path_cp)==0) {
		return -1;
	}

	int num_delimit = 0;
	const char delimit[2] = "/";
	char* token = "root";
	union fs_block block;
  
	for(int i=0;i<strlen(dir_path);i++) {
		if(dir_path[i]=='/' && i<strlen(dir_path)-1) {
			num_delimit++;
		}
	}
	if(dir_path[0]!='/') {
		num_delimit++;
	}

	disk_read(0,block.data);
	int num_inode_blocks = block.super.ninodeblocks;

	struct fs_inode par_inode;
	
	if(num_delimit>1) {
		for(int i=0;i<num_delimit-1;i++) {
			if(i==0) {
				token = strtok(dir_path, delimit);
			} else {
				token = strtok(NULL, delimit);
			}
		}
		par_inode = get_dir_inode(token, num_inode_blocks);
	} else {
		disk_read(1,block.data);
		par_inode = block.inode[0];

	}

	int num_records = par_inode.size;
	if(num_records == DIR_ENTRIES_PER_BLOCK-1) {
		return -1;
	}
	disk_read(par_inode.indirect, block.data);
	char* dir_name;
	if(num_delimit>1) {
		dir_name = strtok(NULL, delimit);
    } else {
    	dir_name = strtok(dir_path, delimit);
    }
	strcpy(block.dir[num_records].name,dir_name);

	//update parent dir entry data
	int inode_num = fs_get_vacant_inode(dir_name, num_inode_blocks);
	block.dir[num_records].inode_num = inode_num;
	block.dir[num_records].type      = 1;
	disk_write(par_inode.indirect, block.data);
    
	//update parent dir inode data
	for(int i=1; i<=num_inode_blocks; i++) {
		disk_read(i, block.data);
		for(int j=0;j<INODES_PER_BLOCK; j++) {
			if(block.inode[j].isvalid == 2 && strcmp(block.inode[j].attr.dir_name,token)==0) {
				block.inode[j].size++;
				disk_write(i, block.data);	
			}
		}
	}

	return 0;
   
}

//updates parent directory inode structure data after deletion of one of its directories
int update_parent_inode_data_after_deletion(int dir_inode_no) {
	union fs_block block;
	union fs_block dir_block;
	disk_read(0,block.data);
	int num_inode_blocks = block.super.ninodeblocks;
	
    
	for(int bl=1; bl<= num_inode_blocks; bl++) {
		disk_read(bl, block.data);
		for(int i=0;i<INODES_PER_BLOCK;i++) {
			if(block.inode[i].isvalid==2) {
				int sz = block.inode[i].size;
				if(sz>0) {
					disk_read(block.inode[i].indirect, dir_block.data);
					for(int j=0; j<sz; j++) {
						if(dir_block.dir[j].inode_num == dir_inode_no) {
							for(int k=j+1;k<sz;k++) {
								dir_block.dir[k-1] = dir_block.dir[k];
							}
							disk_write(block.inode[i].indirect, dir_block.data);
							block.inode[i].size--;
							disk_write(bl, block.data);
							return 0;
						}
					}

				}
			}
		}
	}
	return -1;
}

int fs_delete_dir(int dir_inode_no) {

	if(is_mounted == 0) {
		printf("File system not mounted\n");
		return -1;
	}
	if(dir_inode_no == 0) {
		printf("Cannot delete root directory\n");
		return -1;
	}
	union fs_block block;
	union fs_block dir_block;

	


	int dir_inode_block_idx = dir_inode_no % INODES_PER_BLOCK;
	int dir_block_idx       = dir_inode_no/INODES_PER_BLOCK+1;
	if(update_parent_inode_data_after_deletion(dir_inode_no)==-1) {
		return -1;
	}
	disk_read(dir_block_idx, block.data);

	

	int dir_entry_block_idx = block.inode[dir_inode_block_idx].indirect;
	int dir_entry_sz        = block.inode[dir_inode_block_idx].size;
    //update paren

	if(dir_entry_sz>0) {
		disk_read(dir_entry_block_idx, dir_block.data);
		for(int i=0;i<dir_entry_sz;i++) {
			if(block.dir[i].type==1) { //directory
				fs_delete_dir(dir_block.dir[i].inode_num);
			} else {
				fs_delete(dir_block.dir[i].inode_num, dir_inode_no);
			}
		}
	}
	disk_read(dir_block_idx, block.data); //re reading the block to sync the changes with file deletion
	block.inode[dir_inode_block_idx].size = 0;
	block.inode[dir_inode_block_idx].isvalid = 0;
	for(int i=0; i<POINTERS_PER_INODE;i++) {
		block.inode[dir_inode_block_idx].attr.direct[i]=0;
	}
	block.inode[dir_inode_block_idx].indirect = 0;
	disk_write(dir_block_idx, block.data);
	return 0;
}
