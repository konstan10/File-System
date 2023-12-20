#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fs.h"
#include "disk.h"

#define SIZE 16777200
int main() {
    char* disk_name = "disk1";
    char* file_name = "file1";
    
    make_fs(disk_name);
    mount_fs(disk_name);

    fs_create(file_name);  

    int fd = fs_open(file_name);

    char buffer1[SIZE];
    for(int i = 0; i < SIZE; i++){
        buffer1[i] = 'x';
    }
    char buffer2[SIZE];
    fs_write(fd, buffer1, sizeof(buffer1));

    fs_read(fd, buffer2, sizeof(buffer2));

    for(int i = 0; i < SIZE; i++){
        printf("%d: %c\n", i, buffer2[i]);
    }
     
    
    fs_close(fd); 

    umount_fs(disk_name); 
  
  return 0;
}