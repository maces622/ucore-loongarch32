#ifndef __KERN_FS_FILE_H__
#define __KERN_FS_FILE_H__

//#include <types.h>
#include <fs.h>
#include <proc.h>
#include <atomic.h>
#include <assert.h>
#include <pmm.h>
// #include <ulib.h>
#include <proc.h>
#include <vmm.h>

struct inode;
struct stat;
struct dirent;

// ————————————————
// file.h，抽象成一个文件了。

#define PIPESIZE 512

struct pipe {
    char data[PIPESIZE];
    int nread;     // number of bytes read
    int nwrite;    // number of bytes written
    int readopen;   // read fd is still open
    int writeopen;  // write fd is still open
};

struct file {
    enum {
        FD_NONE, FD_INIT, FD_OPENED, FD_CLOSED,FD_PIPE
    } status;
    bool readable;
    bool writable;
    int fd;
    off_t pos;
    struct inode *node;
    struct pipe *pipe; // FD_PIPE

    atomic_t open_count;
};


/*





// ————————————————

*/

void filemap_init(struct file *filemap);
void filemap_open(struct file *file);
void filemap_close(struct file *file);
void filemap_dup(struct file *to, struct file *from);
bool file_testfd(int fd, bool readable, bool writable);

int file_open(char *path, uint32_t open_flags);
int file_close(int fd);
int file_read(int fd, void *base, size_t len, size_t *copied_store);
int file_write(int fd, void *base, size_t len, size_t *copied_store);
int file_seek(int fd, off_t pos, int whence);
int file_fstat(int fd, struct stat *stat);
int file_fsync(int fd);
int file_getdirentry(int fd, struct dirent *dirent);
int file_dup(int fd1, int fd2);
int file_pipe(int fd[]);
int file_mkfifo(const char *name, uint32_t open_flags);

int pipealloc(struct file *f0, struct file *f1);
void pipeclose(struct pipe *pi, int writable);
int pipewrite(struct pipe *pi, uint64_t addr, int n);
int piperead(struct pipe *pi, uint64_t addr, int n);

static inline int
fopen_count(struct file *file) {
    return atomic_read(&(file->open_count));
}

static inline int
fopen_count_inc(struct file *file) {
    return atomic_add_return(&(file->open_count), 1);
}

static inline int
fopen_count_dec(struct file *file) {
    return atomic_sub_return(&(file->open_count), 1);
}

#endif /* !__KERN_FS_FILE_H__ */

