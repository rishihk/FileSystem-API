/*
    implementation of API
*/

#include "def.h"

pthread_mutex_t mutex_for_fs_stat;

//initialize file system - should be called as the first thing before accessing this file system 
int RSFS_init(){

    //initialize data blocks
    for(int i=0; i<NUM_DBLOCKS; i++){
      void *block = malloc(BLOCK_SIZE); //a data block is allocated from memory
      if(block==NULL){
        printf("[init] fails to init data_blocks\n");
        return -1;
      }
      data_blocks[i] = block;  
    } 

    //initialize bitmaps
    for(int i=0; i<NUM_DBLOCKS; i++) data_bitmap[i]=0;
    pthread_mutex_init(&data_bitmap_mutex,NULL);
    for(int i=0; i<NUM_INODES; i++) inode_bitmap[i]=0;
    pthread_mutex_init(&inode_bitmap_mutex,NULL);    

    //initialize inodes
    for(int i=0; i<NUM_INODES; i++){
        inodes[i].length=0;
        for(int j=0; j<NUM_POINTER; j++) 
            inodes[i].block[j]=-1; //pointer value -1 means the pointer is not used
        inodes[i].num_current_reader=0;
        pthread_mutex_init(&inodes[i].rw_mutex,NULL);
        pthread_mutex_init(&inodes[i].read_mutex,NULL);
    }
    pthread_mutex_init(&inodes_mutex,NULL); 

    //initialize open file table
    for(int i=0; i<NUM_OPEN_FILE; i++){
        struct open_file_entry entry=open_file_table[i];
        entry.used=0; //each entry is not used initially
        pthread_mutex_init(&entry.entry_mutex,NULL);
        entry.position=0;
        entry.access_flag=-1;
    }
    pthread_mutex_init(&open_file_table_mutex,NULL); 

    //initialize root directory
    root_dir.head = root_dir.tail = NULL;

    //initialize mutex_for_fs_stat
    pthread_mutex_init(&mutex_for_fs_stat,NULL);

    //return 0 means success
    return 0;
}

//create file
//if file does not exist, create the file and return 0;
//if file_name already exists, return -1; 
//otherwise, return -2.
int RSFS_create(char *file_name){

    //search root_dir for dir_entry matching provided file_name
    struct dir_entry *dir_entry = search_dir(file_name);

    if(dir_entry){//already exists
        printf("[create] file (%s) already exists.\n", file_name);
        return -1;
    }else{

        if(DEBUG) printf("[create] file (%s) does not exist.\n", file_name);

        //construct and insert a new dir_entry with given file_name
        dir_entry = insert_dir(file_name);
        if(DEBUG) printf("[create] insert a dir_entry with file_name:%s.\n", dir_entry->name);
        
        //access inode-bitmap to get a free inode 
        int inode_number = allocate_inode();
        if(inode_number<0){
            printf("[create] fail to allocate an inode.\n");
            return -2;
        } 
        if(DEBUG) printf("[create] allocate inode with number:%d.\n", inode_number);

        //save inode-number to dir-entry
        dir_entry->inode_number = inode_number;
        
        return 0;
    }
}

// helper function to allocate a free inode
void lock_inode(struct inode *inode, int access_flag) { 
    if (access_flag == RSFS_RDWR) { // if access_flag is RSFS_RDWR
        pthread_mutex_lock(&inode->rw_mutex); // lock the rw_mutex
    } else {  // if access_flag is other
        pthread_mutex_lock(&inode->read_mutex); // lock the read_mutex
        while (inode->num_current_reader < 0) { // while the number of current readers is less than 0
            pthread_mutex_unlock(&inode->read_mutex); // unlock the read_mutex
            pthread_mutex_lock(&inode->rw_mutex); // lock the rw_mutex
            pthread_mutex_unlock(&inode->rw_mutex); // unlock the rw_mutex
            pthread_mutex_lock(&inode->read_mutex); // lock the read_mutex
        }
        inode->num_current_reader++; // increment the number of current readers
        if (inode->num_current_reader == 1) { // if the number of current readers is 1
            pthread_mutex_lock(&inode->rw_mutex); // lock the rw_mutex
        }
        pthread_mutex_unlock(&inode->read_mutex); // unlock the read_mutex
    }
}

// helper function to deallocate an inode
void unlock_inode(struct inode *inode, int access_flag) {
    if (access_flag == RSFS_RDWR) { // if access_flag is RSFS_RDWR
        pthread_mutex_unlock(&inode->rw_mutex); // unlock the rw_mutex
    } else {  // if access_flag is other
        pthread_mutex_lock(&inode->read_mutex); // lock the read_mutex
        inode->num_current_reader--; // decrement the number of current readers
        if (inode->num_current_reader == 0) { // if the number of current readers is 0
            pthread_mutex_unlock(&inode->rw_mutex); // unlock the rw_mutex
        }
        pthread_mutex_unlock(&inode->read_mutex); // unlock the read_mutex
    }
}

//open a file with RSFS_RDONLY or RSFS_RDWR flags
//When flag=RSFS_RDONLY: 
//  if the file is currently opened with RSFS_RDWR (by a process/thread)=> the caller should be blocked (wait); 
//  otherwise, the file is opened and the descriptor (i.e., index of the open_file_entry in the open_file_table) is returned
//When flag=RSFS_RDWR:
//  if the file is currently opened with RSFS_RDWR (by a process/thread) or RSFS_RDONLY (by one or multiple processes/threads) 
//      => the caller should be blocked (i.e. wait);
//  otherwise, the file is opened and the desrcriptor is returned
int RSFS_open(char *file_name, int access_flag) {

    struct dir_entry *de = search_dir(file_name); // search for the directory entry with the given file name
    if (access_flag != RSFS_RDONLY && access_flag != RSFS_RDWR || !de) { // if the access flag is not RSFS_RDONLY or RSFS_RDWR or the directory entry is not found
        return -1; // return failure
    }
    int inode_number = de->inode_number; // get the inode number from the directory entry
    struct inode *inode = &inodes[inode_number]; // get the inode from the inode number
    lock_inode(inode, access_flag); // lock the inode with the given access flag
    int fd = allocate_open_file_entry(access_flag, de); // allocate an open file entry with the given access flag and directory entry
    if (fd < 0) { // if the file descriptor is less than 0
        unlock_inode(inode, access_flag);   // unlock the inode with the given access flag
        return -1; // return failure
    }
    return fd; // return the file descriptor
}

//append the content in buf to the end of the file of descriptor fd
int RSFS_append(int fd, void *buf, int size) {

    struct open_file_entry *ofe = &open_file_table[fd]; // get the open file entry with the given file descriptor
    if (fd < 0 || fd >= NUM_OPEN_FILE || size <= 0 || !ofe->used || ofe->access_flag != RSFS_RDWR) { // if the file descriptor is less than 0, the file descriptor is greater than or equal to the number of open files, the size is less than or equal to 0, the open file entry is not used, or the access flag is not RSFS_RDWR
        return -1; // return failure
    }
    struct dir_entry *de = ofe->dir_entry; // get the directory entry from the open file entry
    int ino_num = de->inode_number; // get the inode number from the directory entry
    struct inode *ino = &inodes[ino_num]; // get the inode from the inode number
    int pos = ofe->position; // get the position from the open file entry

    int appended = 0; // initialize the number of bytes appended to 0

    while (appended < size) { // while the number of bytes appended is less than the size
        int blk_off = pos % BLOCK_SIZE; // get the block offset from the position by modulo BLOCK_SIZE
        int to_write = BLOCK_SIZE - blk_off; // get the number of bytes to write by subtracting the block offset from BLOCK_SIZE
        if (to_write > size - appended) { // if the number of bytes to write is greater than the size minus the number of bytes appended
            to_write = size - appended; // set the number of bytes to write to the size minus the number of bytes appended
        }
        int blk_idx = pos / BLOCK_SIZE; // get the block index from the position by dividing by BLOCK_SIZE
        if (blk_idx >= NUM_POINTER || ino->block[blk_idx] == -1) { // if the block index is greater than or equal to NUM_POINTER or the block at the block index is -1
            int new_blk = allocate_data_block();  // allocate a new data block
            if (new_blk != -1) ino->block[blk_idx] = new_blk;
            else break;

        }
        void *blk = data_blocks[ino->block[blk_idx]]; // get the block from the data blocks at the block index
        for (int i = 0; i < to_write; i++) { // for each byte to write
            ((char*)blk)[blk_off + i] = ((char*)buf)[appended + i]; // copy the byte from the buffer to the block
        }
        pos += to_write; // increment the position by the number of bytes to write
        appended += to_write; // increment the number of bytes appended by the number of bytes to write
    }

    pthread_mutex_lock(&inodes_mutex); // lock the inodes mutex
    ino->length = (pos > ino->length) ? pos : ino->length; // set the length of the inode to the maximum of the position and the length of the inode
    ofe->position = pos; // set the position of the open file entry to the position
    pthread_mutex_unlock(&inodes_mutex); // unlock the inodes mutex

    return appended;
}

//update current position of the file (which is in the open_file_entry) to offset
int RSFS_fseek(int fd, int offset) {

    struct open_file_entry *entry = &open_file_table[fd]; // get the open file entry with the given file descriptor
    if (fd < 0 || fd >= NUM_OPEN_FILE ||!entry->used) { // if the file descriptor is less than 0, the file descriptor is greater than or equal to the number of open files, or the open file entry is not used
        return -1; // return failure
    }
    struct dir_entry *dir_entry = entry->dir_entry; // get the directory entry from the open file entry
    int inode_number = dir_entry->inode_number; // get the inode number from the directory entry
    struct inode *inode = &inodes[inode_number]; // get the inode from the inode number
    int position = entry->position; // get the position from the open file entry
    int length = inode->length; // get the length of the inode
    if (offset < 0 || offset > length) { // if the offset is less than 0 or the offset is greater than the length
        return position; // return the position
    }
    entry->position = offset; // set the position of the open file entry to the offset

    return offset; // return the offset
}

//read from file from the current position for up to size bytes
int RSFS_read(int fd, void *buf, int size) {
    struct open_file_entry *ofe = &open_file_table[fd]; // get the open file entry with the given file descriptor
    if (fd < 0 || fd >= NUM_OPEN_FILE || size <= 0 || !ofe->used) { // if the file descriptor is less than 0, the file descriptor is greater than or equal to the number of open files, the size is less than or equal to 0, or the open file entry is not used
        return -1;
    }
    struct dir_entry *de = ofe->dir_entry; // get the directory entry from the open file entry
    int ino_num = de->inode_number; // get the inode number from the directory entry
    struct inode *ino = &inodes[ino_num]; // get the inode from the inode number
    int pos = ofe->position; // get the position from the open file entry

    int read = 0; // initialize the number of bytes read to 0
    while (read < size && pos < ino->length) { // while the number of bytes read is less than the size and the position is less than the length of the inode
        int blk_off = pos % BLOCK_SIZE; // get the block offset from the position by modulo BLOCK_SIZE
        int blk_idx = pos / BLOCK_SIZE; // get the block index from the position by dividing by BLOCK_SIZE
        int to_read = BLOCK_SIZE - blk_off; // get the number of bytes to read by subtracting the block offset from BLOCK_SIZE
        to_read = (to_read > size - read) ? (size - read) : to_read; // get the minimum of the number of bytes to read if the bytes is greater than the size minus the bytes
        to_read = (to_read > ino->length - pos) ? (ino->length - pos) : to_read; // get the minimum of the number of bytes to read if the length of the inode is less than the position
        void *blk = data_blocks[ino->block[blk_idx]]; // get the block from the data blocks at the block index
        char *src_ptr = (char *)blk + blk_off; // get the source pointer from the block and block offset
        char *dst_ptr = (char *)buf + read; // get the destination pointer from the buffer and number of bytes read
        for (int i = 0; i < to_read; i++) { // for each byte to read
            dst_ptr[i] = src_ptr[i]; // copy the byte from the source pointer to the destination pointer
        }
        pos += to_read; // increment the position by the number of bytes to read
        read += to_read; // increment the number of bytes read by the number of bytes to read
    }
    ofe->position = pos; // set the position of the open file entry to the position
    return read; // return the number of bytes read
}

//close file: return 0 if succeed
int RSFS_close(int fd) {
    struct open_file_entry *ofe = &open_file_table[fd]; // get the open file entry with the given file descriptor
    if (fd < 0 || fd >= NUM_OPEN_FILE || !ofe->used) { // if the file descriptor is less than 0, the file descriptor is greater than or equal to the number of open files, or the open file entry is not used
        return -1; // return failure
    }
    struct dir_entry *de = ofe->dir_entry; // get the directory entry from the open file entry
    int ino_num = de->inode_number; // get the inode number from the directory entry
    struct inode *ino = &inodes[ino_num]; // get the inode from the inode number
    if (ofe->access_flag == RSFS_RDWR) { // if the access flag is RSFS_RDWR
        pthread_mutex_unlock(&ino->rw_mutex); // unlock the rw_mutex
    } else { // if the access flag is other
        pthread_mutex_lock(&ino->read_mutex); // lock the read_mutex
        ino->num_current_reader--; // decrement the number of current readers
        if (ino->num_current_reader == 0) { // if the number of current readers is 0
            pthread_mutex_unlock(&ino->read_mutex); // unlock the read_mutex
            pthread_mutex_unlock(&ino->rw_mutex); // unlock the rw_mutex
        } else { // if the number of current readers is not 0
            pthread_mutex_unlock(&ino->read_mutex); // unlock the read_mutex
        }
    }
    free_open_file_entry(fd); // free the open file entry with the given file descriptor

    return 0; // return success
}

//delete file
int RSFS_delete(char *file_name) {
    struct dir_entry *de = search_dir(file_name); // search for the directory entry with the given file name
    if (!de) { // if the directory entry is not found
        return -1; // return failure
    }
    int ino_num = de->inode_number; // get the inode number from the directory entry
    struct inode *ino = &inodes[ino_num]; // get the inode from the inode number
    for (int i = 0; i < NUM_POINTER; i++) { // for each pointer
        if (ino->block[i] != -1) { // if the block at the pointer is not -1
            free_data_block(ino->block[i]); // free the data block at the pointer
            ino->block[i] = -1; // set the block at the pointer to -1
        }
    }
    free_inode(ino_num); // free the inode with the inode number
    delete_dir(file_name); // delete the directory entry with the given file name
    return 0; // return success
}

int RSFS_write(int fd, void *buf, int size) {
    struct open_file_entry *ofe = &open_file_table[fd]; // get the open file entry with the given file descriptor
    if (fd < 0 || fd >= NUM_OPEN_FILE || size <= 0 || !ofe->used || ofe->access_flag != RSFS_RDWR) { // if the file descriptor is less than 0, the file descriptor is greater than or equal to the number of open files, the size is less than or equal to 0, the open file entry is not used, or the access flag is not RSFS_RDWR
        return -1; // return failure
    }
    struct dir_entry *de = ofe->dir_entry; // get the directory entry from the open file entry
    int ino_num = de->inode_number; // get the inode number from the directory entry
    struct inode *ino = &inodes[ino_num]; // get the inode from the inode number
    int pos = ofe->position; // get the position from the open file entry

    int written = 0; // initialize the number of bytes written to 0
    while (written < size) { // while the number of bytes written is less than the size
        int blk_off = pos % BLOCK_SIZE; // get the block offset from the position by modulo BLOCK_SIZE
        int blk_idx = pos / BLOCK_SIZE; // get the block index from the position by dividing by BLOCK_SIZE
        int to_write = BLOCK_SIZE - blk_off; // get the number of bytes to write by subtracting the block offset from BLOCK_SIZE
        if (to_write > size - written) { // if the number of bytes to write is greater than the size minus the number of bytes written
            to_write = size - written; // set the number of bytes to write to the size minus the number of bytes written
        }
        if (blk_idx >= NUM_POINTER || ino->block[blk_idx] == -1) { // if the block index is greater than or equal to NUM_POINTER or the block at the block index is -1
            int new_blk = allocate_data_block(); // allocate a new data block
            if (new_blk != -1) ino->block[blk_idx] = new_blk;
            else break;
        }
        void *blk = data_blocks[ino->block[blk_idx]]; // get the block from the data blocks at the block index
        char *src_ptr = (char *)buf + written; // get the source pointer from the buffer and number of bytes written
        char *dst_ptr = (char *)blk + blk_off; // get the destination pointer from the block and block offset
        for (int i = 0; i < to_write; i++) {   // for each byte to write
            dst_ptr[i] = src_ptr[i]; // copy the byte from the source pointer to the destination pointer
        }
        pos += to_write; // increment the position by the number of bytes to write
        written += to_write; // increment the number of bytes written by the number of bytes to write
    }
    pthread_mutex_lock(&inodes_mutex); // lock the inodes mutex
    ofe->position = pos; // set the position of the open file entry to the position
    if (pos > ino->length) { // if the position is greater than the length of the inode
        ino->length = pos; // set the length of the inode to the position
    }
    pthread_mutex_unlock(&inodes_mutex); // unlock the inodes mutex
    return written;  // return the number of bytes written
}

int RSFS_cut(int fd, int size) {
    struct open_file_entry *ofe = &open_file_table[fd]; // get the open file entry with the given file descriptor
    if (fd < 0 || fd >= NUM_OPEN_FILE || size <= 0 || ofe->access_flag != RSFS_RDWR) { // if the file descriptor is less than 0, the file descriptor is greater than or equal to the number of open files, the size is less than or equal to 0, or the access flag is not RSFS_RDWR
        return -1; // return failure
    }
    pthread_mutex_lock(&ofe->entry_mutex); // lock the entry mutex
    if (ofe->used == 0) { // if the open file entry is not used
        pthread_mutex_unlock(&ofe->entry_mutex); // unlock the entry mutex
        return -1; // return failure
    }
    int pos = ofe->position; // get the position from the open file entry
    struct dir_entry *de = ofe->dir_entry; // get the directory entry from the open file entry
    struct inode *ino = &inodes[de->inode_number]; // get the inode from the directory entry

    int to_cut = (size < ino->length - pos) ? size : (ino->length - pos); // get the number of bytes to cut
    int src_blk = (pos + to_cut) / BLOCK_SIZE; // get the source block from the position plus the number of bytes to cut by dividing by BLOCK_SIZE
    int dst_blk = pos / BLOCK_SIZE; // get the destination block from the position by dividing by BLOCK_SIZE
    int src_off = (pos + to_cut) % BLOCK_SIZE; // get the source offset from the position plus the number of bytes to cut by modulo BLOCK_SIZE
    int dst_off = pos % BLOCK_SIZE; // get the destination offset from the position by modulo BLOCK_SIZE
    int to_move = ino->length - (pos + to_cut); // get the number of bytes to move

    while (to_move > 0) { // while the number of bytes to move is greater than 0
        int in_src_blk = (to_move < BLOCK_SIZE - src_off) ? to_move : (BLOCK_SIZE - src_off); // get the number of bytes in the source block
        int in_dst_blk = (to_move < BLOCK_SIZE - dst_off) ? to_move : (BLOCK_SIZE - dst_off); // get the number of bytes in the destination block
        int to_copy = (in_src_blk < in_dst_blk) ? in_src_blk : in_dst_blk; // get the number of bytes to copy

        char *src_ptr = (char *)data_blocks[ino->block[src_blk]] + src_off; // get the source pointer from the data blocks at the source block and source offset
        char *dst_ptr = (char *)data_blocks[ino->block[dst_blk]] + dst_off; // get the destination pointer from the data blocks at the destination block and destination offset
        for (int i = 0; i < to_copy; i++) { // for each byte to copy
            dst_ptr[i] = src_ptr[i]; // copy the byte from the source pointer to the destination pointer
        }
        src_off += to_copy; // increment the source offset by the number of bytes to copy
        to_move -= to_copy; // decrement the number of bytes to move by the number of bytes to copy
        dst_off += to_copy; // increment the destination offset by the number of bytes to copy
        src_off == BLOCK_SIZE ? (src_blk++, src_off = 0) : (void)0; // if the source offset is BLOCK_SIZE, increment the source block and set the source offset to 0
        dst_off == BLOCK_SIZE ? (dst_blk++, dst_off = 0) : (void)0; // if the destination offset is BLOCK_SIZE, increment the destination block and set the destination offset to 0

    }
    ino->length -= to_cut; // decrement the length of the inode by the number of bytes to cut
    int new_last_blk = (ino->length - 1) / BLOCK_SIZE; // get the new last block from the length minus 1 by dividing by BLOCK_SIZE
    for (int i = new_last_blk + 1; i < NUM_POINTER; i++) { // for each block after the new last block
        if (ino->block[i] != -1) { // if the block at the block index is not -1
            free_data_block(ino->block[i]); // free the data block at the block index
            ino->block[i] = -1; // set the block at the block index to -1
        }
    }
    pthread_mutex_unlock(&ofe->entry_mutex); // unlock the entry mutex
    return to_cut; // return the number of bytes cut
}

void RSFS_stat(){
    pthread_mutex_lock(&mutex_for_fs_stat);
    printf("\nCurrent status of the file system:\n\n %16s%10s%10s\n", "File Name", "Length", "iNode #");

    //list files
    struct dir_entry *dir_entry = root_dir.head;
    while(dir_entry!=NULL){

        int inode_number = dir_entry->inode_number;
        struct inode *inode = &inodes[inode_number];
        
        printf("%16s%10d%10d\n", dir_entry->name, inode->length, inode_number);
        dir_entry = dir_entry->next;
    }
    
    //data blocks
    int db_used=0;
    for(int i=0; i<NUM_DBLOCKS; i++) db_used+=data_bitmap[i];
    printf("\nTotal Data Blocks: %4d,  Used: %d,  Unused: %d\n", NUM_DBLOCKS, db_used, NUM_DBLOCKS-db_used);

    //inodes
    int inodes_used=0;
    for(int i=0; i<NUM_INODES; i++) inodes_used+=inode_bitmap[i];
    printf("Total iNode Blocks: %3d,  Used: %d,  Unused: %d\n", NUM_INODES, inodes_used, NUM_INODES-inodes_used);

    //open files
    int of_num=0;
    for(int i=0; i<NUM_OPEN_FILE; i++) of_num+=open_file_table[i].used;
    printf("Total Opened Files: %3d\n\n", of_num);
    pthread_mutex_unlock(&mutex_for_fs_stat);
}