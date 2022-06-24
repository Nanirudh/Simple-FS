# Simple-FS
Simple unix-like file system in user space to understand the concepts of FSM (file system management).Written in C programming language <br>
It supports file and directory management (create, delete, copy data ).Inode no of root directory is 0. 

## Usage Instructions (Commands in order)
1. make
2. ./simplefs <diskfile> <no of blocks in diskfile>
Eg. ./simplefs image.20 20

3. format              # formats the disk file
4. mount               # mounts the filesystem and creates bitmap

Supported commands<br>
```
debug                                            prints the filesystem contents (directories and files) in hierarchial representation <br>
create <parent dir inode no> <file name>         creates a file. Input: parent directory inode no and file name <br>
delete  <inode> <parent dir inode>               deletes a file. Input: inode of file and parent directory inode <br>
cat <inode>                                      prints the contents of a file to stdout. Input: inode of file <br>
copyin  <file> <inode>                           copies the contents in file to file pointed by inode. Input: filename and inode no
copyout <inode> <file>                           copies the contents from the file pointed by inode to file. Input: inode no and file name
mkdir <path>                                     path of the directory to be created . Input: path Eg./test
help                                             lists out all the commands with arguments
quit
exit
```
Sample debug output:<br>
![fs_debug](https://user-images.githubusercontent.com/40365086/175609584-172063e3-cdba-4019-855f-00d9d19f29cb.png)

Ref: [www-users.cselabs.umn.edu/classes/Fall-2019/csci5103/tmp/project3/project3](https://www-users.cselabs.umn.edu/classes/Fall-2019/csci5103/tmp/project3/project3.html)





