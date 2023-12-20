#include "disk.h"
#include "fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define BLOCK_SIZE 4096
#define MAX_DATA_BLOCKS 4096
#define MAX_BLOCKS 8192
#define MAX_FILES 64
#define MAX_FILE_DES 32
#define MAX_FILE_NAME 16
#define MAX_FILE_SIZE 16777216

typedef struct super_block {
    int fat_idx; // First block of the FAT
    int fat_len; // Length of FAT in blocks
    int dir_idx; // First block of directory
    int dir_len; // Length of directory in blocks
    int data_idx; // First block of file-data
} super_block;

typedef struct dir_entry {
    int used; // Is this file-”slot” in use
    char name[MAX_FILE_NAME]; // DOH!
    int size; // file size
    int* head; // first data block of file
    int ref_cnt; // how many open file descriptors are there? ref_cnt > 0 -> cannot delete file
} dir_entry;

typedef struct file_descriptor {
    int used; // fd in use
    int* file; // the first block of the file (f) to which fd refers too
    int offset; // position of fd within f in bytes
} fd;

super_block fs;
fd file_descriptors[MAX_FILE_DES]; // 32
int FAT[MAX_DATA_BLOCKS]; // Will be populated with the FAT data
dir_entry directory[MAX_FILES]; // Will be populated with the directory data

int make_fs(char* disk_name) {
    if (make_disk(disk_name) == -1) {
        return -1;
    }
    if (open_disk(disk_name) == -1) {
        return -1;
    }

    fs.fat_idx = 1;
    fs.fat_len = ceil((double)MAX_DATA_BLOCKS * sizeof(int) / BLOCK_SIZE);
    fs.dir_idx = fs.fat_idx + fs.fat_len;
    fs.dir_len = ceil((double)MAX_FILES * sizeof(dir_entry) / BLOCK_SIZE);
    fs.data_idx = 4096;

    for (int i = 0; i < MAX_FILES; i++) {
        directory[i].used = 0;
        directory[i].head = malloc(sizeof(int));
        *(directory[i].head) = 4096 + i;
        directory[i].name[0] = '\0';
    }

    for (int i = 0; i < MAX_DATA_BLOCKS; i++) {
        FAT[i] = -42;
    }

    char* fs_buf = malloc(BLOCK_SIZE);
    memcpy(fs_buf, &fs, sizeof(super_block));
    block_write(0, fs_buf);

    char* fat_buf = malloc(MAX_DATA_BLOCKS * sizeof(int));
    for (int i = 0; i < MAX_DATA_BLOCKS; i++) {
        memcpy(fat_buf + i * sizeof(int), &FAT[i], sizeof(int));
    }

    for (int i = 0; i < fs.fat_len; i++) {
        block_write(fs.fat_idx + i, fat_buf + i * BLOCK_SIZE);
    }

    char* dir_buf = malloc(BLOCK_SIZE);
    memcpy(dir_buf, &directory, MAX_FILES * sizeof(dir_entry));
    block_write(fs.dir_idx, dir_buf);

    free(fs_buf);
    free(fat_buf);
    free(dir_buf);

    if (close_disk(disk_name) == -1) {
        return -1;
    }
    return 0;
}

int mount_fs(char* disk_name) {
    if (open_disk(disk_name) == -1) {
        return -1;
    }

    char* fs_buf = malloc(BLOCK_SIZE);
    block_read(0, fs_buf);
    memcpy(&fs, fs_buf, sizeof(super_block));

    char* fat_buf = malloc(MAX_DATA_BLOCKS * sizeof(int));
    for (int i = 0; i < fs.fat_len; i++) {
        block_read(fs.fat_idx + i, fat_buf + i * BLOCK_SIZE);
    }
    for (int i = 0; i < MAX_DATA_BLOCKS; i++) {
        memcpy(&FAT[i], fat_buf + i * sizeof(int), sizeof(int));
    }

    char* dir_buf = malloc(BLOCK_SIZE);
    block_write(fs.dir_idx, dir_buf);
    memcpy(&directory, dir_buf, MAX_FILES * sizeof(dir_entry));

    for (int i = 0; i < MAX_FILE_DES; i++) {
        file_descriptors[i].used = 0;
        file_descriptors[i].file = NULL;
        file_descriptors[i].offset = 0;
    }
    free(fs_buf);
    free(fat_buf);
    free(dir_buf);
    return 0;
}

int umount_fs(char* disk_name) {
    char* fs_buf = malloc(BLOCK_SIZE);
    memcpy(fs_buf, &fs, sizeof(super_block));
    block_write(0, fs_buf);

    char* fat_buf = malloc(MAX_DATA_BLOCKS * sizeof(int));
    for (int i = 0; i < MAX_DATA_BLOCKS; i++) {
        memcpy(fat_buf + i * sizeof(int), &FAT[i], sizeof(int));
    }

    for (int i = 0; i < fs.fat_len; i++) {
        block_write(fs.fat_idx + i, fat_buf + i * BLOCK_SIZE);
    }

    char* dir_buf = malloc(BLOCK_SIZE);
    memcpy(dir_buf, &directory, MAX_FILES * sizeof(dir_entry));
    block_write(fs.dir_idx, dir_buf);

    free(fs_buf);
    free(fat_buf);
    free(dir_buf);

    if (close_disk(disk_name) == -1) {
        return -1;
    }
    return 0;
}

int fs_open(char* name) {
    int i;
    for (i = 0; i < MAX_FILES; i++) {
        if (strcmp(name, directory[i].name) == 0) {
            break;
        }
    }
    if (i < MAX_FILES) {
        for (int j = 0; j < MAX_FILE_DES; j++) {
            if (file_descriptors[j].used == 0) {
                file_descriptors[j].used = 1;
                file_descriptors[j].file = directory[i].head;
                directory[i].ref_cnt++;
                return j;
            }
        }
    }
    return -1;
}

int fs_close(int fildes) {
    if (fildes < 0 || fildes > MAX_FILE_DES - 1) {
        return -1;
    } 
    else if (file_descriptors[fildes].used == 0) {
        return -1;
    } 
    else {
        int i;
        for (i = 0; i < MAX_FILES; i++) {
            if (*(directory[i].head) == *(file_descriptors[fildes].file)) {
                directory[i].ref_cnt--;
                break;
            }
        }
        file_descriptors[fildes].used = 0;
        file_descriptors[fildes].file = NULL;
        file_descriptors[fildes].offset = 0;
        return 0;
    }
}

int fs_create(char* name) {
    if (strlen(name) > MAX_FILE_NAME - 1) {
        return -1;
    }
    int used_count = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        if (strcmp(directory[i].name, name) == 0) {
            return -1;
        }
        if (directory[i].used == 1) {
            used_count++;
        }
    }
    if (used_count == MAX_FILES) {
        return -1;
    }
    for (int i = 0; i < MAX_FILES; i++) {
        if (directory[i].used == 0) {
            directory[i].used = 1;
            strcpy(directory[i].name, name);
            directory[i].ref_cnt = 0;
            directory[i].size = 0;
            directory[i].head = malloc(sizeof(int));
            *(directory[i].head) = 4096 + i;
            return 0;
        }
    }
    return -1;
}

int fs_delete(char* name) {
    int i;
    for (i = 0; i < MAX_FILES; i++) {
        if (directory[i].ref_cnt == 0 && strcmp(directory[i].name, name) == 0) {
            break;
        }
    }
    if (i == MAX_FILES) {
        return -1;
    }
    int index = *(directory[i].head);
    int temp;
    char* null_block = calloc(1, BLOCK_SIZE);
    if (index < MAX_DATA_BLOCKS) {
        while (1) {
            if (FAT[index] == -1) {
                block_write(index, null_block);
                FAT[index] = -42;
                break;
            } 
            else {
                block_write(index, null_block);
                temp = FAT[index];
                FAT[index] = -42;
                index = temp;
            }
        }
    }
    free(null_block);
    *(directory[i].head) = 4096 + i;
    directory[i].used = 0;
    directory[i].size = 0;
    directory[i].name[0] = '\0';
    return 0;
}

int fs_read(int fildes, void* buf, size_t nbyte) {
    if (fildes < 0 || fildes > MAX_FILE_DES - 1) {
        return -1;
    } 
    else if (file_descriptors[fildes].used == 0) {
        return -1;
    } 
    else {
        int i;
        for (i = 0; i < MAX_FILES; i++) {
            if (*(file_descriptors[fildes].file) == *(directory[i].head)) {
                if (directory[i].size == 0) {
                    return 0;
                } 
                else {
                    break;
                }
            }
        }
        char* temp_buf = malloc(directory[i].size);
        int index = *(directory[i].head);
        int j = 0;

        while (1) {
            if (FAT[index] == -1) {
                block_read(4096 + index, temp_buf + j * BLOCK_SIZE);
                break;
            } 
            else {
                block_read(4096 + index, temp_buf + j * BLOCK_SIZE);
                index = FAT[index];
                j++;
            }
        }

        memcpy(buf, temp_buf + file_descriptors[fildes].offset, nbyte);
        free(temp_buf);
        if (file_descriptors[fildes].offset + nbyte > directory[i].size) {
            int old_offset = file_descriptors[fildes].offset;
            file_descriptors[fildes].offset = directory[i].size;
            return directory[i].size - old_offset;
        } 
        else {
            file_descriptors[fildes].offset += nbyte;
            return nbyte;
        }
    }
}

int get_disk_size(){
    int disk_size = 0;
    for(int i = 0; i < MAX_FILES; i++){
        if(directory[i].used == 1){
            disk_size += directory[i].size;
        }
    }
    return disk_size;
}

int fs_write(int fildes, void *buf, size_t nbyte) {
    if (fildes < 0 || fildes > MAX_FILE_DES - 1) {
        return -1;
    } 
    else if (file_descriptors[fildes].used == 0) {
        return -1;
    } 
    else if (get_disk_size() == MAX_FILE_SIZE) {
        return 0;
    } 
    else {
        int i;
        for (i = 0; i < MAX_FILES; i++) {
            if (file_descriptors[fildes].file == directory[i].head) {
                break;
            }
        }
        int blocks_allocated = ceil((double)directory[i].size / BLOCK_SIZE);

        if (file_descriptors[fildes].offset + nbyte > directory[i].size) {
            if (file_descriptors[fildes].offset + nbyte > MAX_FILE_SIZE) {
                nbyte = MAX_FILE_SIZE - file_descriptors[fildes].offset;
                directory[i].size = MAX_FILE_SIZE;
            } 
            else {
                directory[i].size = nbyte + file_descriptors[fildes].offset;
            }
        }

        // read current bytes into a buffer, if there are any that is
        char *temp_buf = malloc(directory[i].size);
        int index;
        if (*(directory[i].head) < 4096) {
            int l = 0;
            index = *(directory[i].head);
            while (1) {
                if (FAT[index] == -1) {
                    block_read(4096 + index, temp_buf + l * BLOCK_SIZE);
                    break;
                } 
                else {
                    block_read(4096 + index, temp_buf + l * BLOCK_SIZE);
                    index = FAT[index];
                    l++;
                }
            }
        }
        // copy bytes of given buffer into temp_buf at offset
        memcpy(temp_buf + file_descriptors[fildes].offset, buf, nbyte);

        int extra_blocks = ceil((double)directory[i].size / BLOCK_SIZE) - blocks_allocated;
        int k = 0;
        int count = 0;
        // setting up the blocks
        if (*(directory[i].head) >= MAX_DATA_BLOCKS) { // never allocated
            for (k = 0; k < MAX_DATA_BLOCKS; k++) {
                if (FAT[k] == -42) {
                    FAT[k] = -1;
                    *(directory[i].head) = k;
                    *(file_descriptors[fildes].file) = k;
                    count++;
                    break;
                }
            }
        } 
        else { // blocks were previously allocated
            k = *(directory[i].head);
            while (1) {
                if (FAT[k] == -1) {
                    break;
                } 
                else {
                    k = FAT[k];
                }
            }
        }
        if (count < extra_blocks) {
            for (int j = 0; j < MAX_DATA_BLOCKS; j++) { // allocate and map more data blocks
                if (FAT[j] == -42) {
                    FAT[k] = j;
                    k = j;
                    count++;
                    if (count == extra_blocks) {
                        break;
                    }
                }
            }
            FAT[k] = -1;
        }

        // write buffer back into blocks
        index = *(directory[i].head);
        int l = 0;
        while (1) {
            if (FAT[index] == -1) {
                block_write(4096 + index, temp_buf + l * BLOCK_SIZE);
                break;
            } 
            else {
                block_write(4096 + index, temp_buf + l * BLOCK_SIZE);
                index = FAT[index];
                l++;
            }
        }
        file_descriptors[fildes].offset += nbyte;
        free(temp_buf);

        return nbyte;
    }
}
int fs_get_filesize(int fildes){
    if(fildes < 0 || fildes > MAX_FILE_DES - 1){
        return -1;
    }
    else if(file_descriptors[fildes].used == 0){
        return -1;
    }
    else{
        for(int i = 0; i < MAX_FILES; i++){
            if(file_descriptors[fildes].file == directory[i].head){
                return directory[i].size;
            }
        }
        return -1;
    }
}
int fs_listfiles(char ***files) {
    char **names = malloc((MAX_FILES + 1) * sizeof(char *));
    if (names == NULL) {
        return -1;
    }
    int count = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        if (directory[i].used == 1) {
            names[count] = strdup(directory[i].name);
            count++;
        }
    }
    names[count] = NULL;
    *files = names;
    return 0;
}
int fs_lseek(int fildes, off_t offset) {
    if (fildes < 0 || fildes > MAX_FILE_DES - 1) {
        return -1;
    } 
    else if (file_descriptors[fildes].used == 0) {
        return -1;
    } 
    else {
        int i;
        for (i = 0; i < MAX_FILES; i++) {
            if (*(file_descriptors[fildes].file) == *(directory[i].head)) {
                break;
            }
        }
        if (offset < 0 || offset > directory[i].size) {
            return -1;
        } else {
            file_descriptors[fildes].offset = offset;
            return 0;
        }
    }
}

int fs_truncate(int fildes, off_t length) {
    if (fildes < 0 || fildes > MAX_FILE_DES - 1) {
        return -1;
    } 
    else if (file_descriptors[fildes].used == 0) {
        return -1;
    } 
    else {
        int i;
        for (i = 0; i < MAX_FILES; i++) {
            if (*(file_descriptors[fildes].file) == *(directory[i].head)) {
                if (directory[i].size > length) {
                    if (file_descriptors[fildes].offset > length) {
                        file_descriptors[fildes].offset = length;
                    }
                    break;
                } 
                else if (directory[i].size == length) {
                    return 0;
                } 
                else {
                    return -1;
                }
            }
        }
        int blocks_allocated = ceil((double)directory[i].size / BLOCK_SIZE);
        char *temp_buf = malloc(directory[i].size);
        directory[i].size = length;
        int index = *(directory[i].head);
        int j = 0;

        while (1) { // write everything into temp_buf
            if (FAT[index] == -1) {
                block_read(4096 + index, temp_buf + j * BLOCK_SIZE);
                break;
            } 
            else {
                block_read(4096 + index, temp_buf + j * BLOCK_SIZE);
                index = FAT[index];
                j++;
            }
        }

        char *null_block = calloc(1, BLOCK_SIZE);
        index = *(directory[i].head);
        j = 0;
        while (1) { // null all of the blocks
            if (FAT[index] == -1) {
                block_write(4096 + index, null_block);
                break;
            } 
            else {
                block_write(4096 + index, null_block);
                index = FAT[index];
                j++;
            }
        }
        free(null_block);
        char *new_buf = malloc(length);
        memcpy(new_buf, temp_buf, length);
        free(temp_buf);
        int new_total_blocks = ceil((double)length / BLOCK_SIZE);
        int count = 0;
        index = *(directory[i].head);

        if (blocks_allocated - new_total_blocks > 0) { // remap the file allocation table if block num needs resizing
            int last_index;
            int array_iter;
            int delete[blocks_allocated - new_total_blocks];
            while (1) {
                if (FAT[index] == -1) {
                    count++;
                    if (count > new_total_blocks) {
                        delete[array_iter] = index;
                        array_iter++;
                    }
                    break;
                } 
                else {
                    count++;
                    if (count == new_total_blocks) {
                        last_index = index;
                        array_iter = 0;
                    }
                    if (count > new_total_blocks) {
                        delete[array_iter] = index;
                        array_iter++;
                    }
                    index = FAT[index];
                }
            }
            FAT[last_index] = -1;
            for (int m = 0; m < blocks_allocated - new_total_blocks; m++) {
                FAT[delete[m]] = -42;
            }
        }

        index = *(directory[i].head);
        int l = 0;
        while (1) {
            if (FAT[index] == -1) {
                block_write(4096 + index, new_buf + l * BLOCK_SIZE);
                break;
            } 
            else {
                block_write(4096 + index, new_buf + l * BLOCK_SIZE);
                index = FAT[index];
                l++;
            }
        }

        free(new_buf);
        return 0;
    }
}
