/*
  * linux.c 
  *
  * Part of diskBench - IO bandwidth measurement
  *
  * Copyright (C) 2010-2011  Amund Elstad <amund.elstad@gmail.com>
  *
  *  This program is free software: you can redistribute it and/or modify
  *  it under the terms of the GNU General Public License as published by
  *   the Free Software Foundation, either version 3 of the License, or
  *  (at your option) any later version.
  *
  *  This program is distributed in the hope that it will be useful,
  *  but WITHOUT ANY WARRANTY; without even the implied warranty of
  *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  *   GNU General Public License for more details.
  *
  *   You should have received a copy of the GNU General Public License
  *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
  */
#include "diskBench.h"
#include <libaio.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <fcntl.h>

struct linux_platform_file {
	int fd;
};

struct linux_platform_queue {
	struct iocb *iocbs;
	struct io_event *events;
	io_context_t ctxp;
};


static apr_size_t linux_get_pagesize() {
	return sysconf(_SC_PAGESIZE);
}

static apr_size_t linux_get_min_io_size() {
    return 512;
}

static apr_status_t linux_create_io_buffer(void **buf, uint64_t size)
{
    *buf = mmap(NULL,size,PROT_READ|PROT_WRITE,MAP_ANONYMOUS|MAP_PRIVATE,-1,0);

    return APR_SUCCESS;
}

static apr_status_t linux_file_open(char *filename, uint64_t *length, double freespace_percentage,
                                    int *file_truncated, struct platform_file **rv)
{
	struct linux_platform_file *file;
	int fd;
	*rv = NULL;
    apr_uint64_t current_length;
    struct stat filestat;
    /* first try to open existing */
	fd = open(filename, O_RDWR, S_IRUSR|S_IWUSR);
    if(fd < 0) {
    	fd = open(filename, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR);
        if(fd < 0)
            return APR_EGENERAL;
                
    }
    /* find current length */
    current_length = lseek(fd, 0, SEEK_END);
    if(fstat(fd, &filestat) < 0)
        return APR_EGENERAL;

    if(!S_ISBLK(filestat.st_mode)) {
        uint64_t free_bytes;
        struct statvfs statvfs;
        if(fstatvfs(fd, &statvfs) < 0) {
            return APR_EGENERAL;
        }
        free_bytes = ((apr_uint64_t) statvfs.f_bsize) * statvfs.f_bavail;

        /* override size if requested is larger than space available */
        if(*length > free_bytes+current_length) {
            *length = (free_bytes + current_length)*freespace_percentage;

            /* round off to  boundary */
            *length = *length - *length % linux_get_pagesize();
        }
        /* reuse current_length if it exists */
        if(current_length > 128*APR_UINT64_C(1048576)) {
            *length = current_length;
        }

       if(*length == 0) {
            /* calculate size based on space available*/
            *length = (apr_uint64_t) (((double) free_bytes+current_length)*freespace_percentage);

            /* round off to 128MB boundary */
            *length /= 128*APR_UINT64_C(1048576);
            *length *= 128*APR_UINT64_C(1048576);
        }
        *file_truncated = *length != current_length;
    } else {
        *length = current_length;
        *file_truncated = 0;
    }

    if(*file_truncated) {
		if(ftruncate(fd, *length))
			return APR_EGENERAL;

		fallocate(fd,0,0,*length);
	}
	close(fd);
	/* Open in O_DIRECT mode */
        fd = open(filename, O_DIRECT|O_RDWR, S_IRUSR|S_IWUSR);
	
	file = malloc(sizeof(struct linux_platform_file));
	file->fd = fd;
	*rv = (struct platform_file*) file;

	return APR_SUCCESS;
}

static apr_status_t linux_file_truncate(struct platform_file *the_file, uint64_t *length)
{
	struct linux_platform_file *file = (struct linux_platform_file *) the_file;
	if(ftruncate(file->fd, *length)) {
	    return APR_EGENERAL;
	}
	return APR_SUCCESS;
}

static apr_status_t linux_file_close(struct platform_file *the_file)
{
	struct linux_platform_file *file = (struct linux_platform_file *) the_file;
	if(close(file->fd))
		return APR_EGENERAL;

	free(file);

	return APR_SUCCESS;
}

static apr_status_t linux_file_flush(struct platform_file *the_file)
{
	struct linux_platform_file *file = (struct linux_platform_file *) the_file;
	if(fsync(file->fd))
		return APR_EGENERAL;

	return APR_SUCCESS;
}

static apr_status_t linux_queue_create(struct async_queue *queue)
{
	struct linux_platform_queue *q;

	q = apr_pcalloc(queue->pool, sizeof(struct linux_platform_queue));
	if(io_setup(queue->total, &(q->ctxp))) {
		return APR_EGENERAL;
	}
	q->iocbs = apr_pcalloc(queue->pool, sizeof(struct iocb)*queue->total);
	q->events = apr_pcalloc(queue->pool, sizeof(struct io_event)*queue->total);


	queue->platform_queue = (struct async_platform_queue*) q;
	return APR_SUCCESS;
}

static apr_status_t linux_queue_destroy(struct async_queue *queue)
{
	struct linux_platform_queue *q = (struct linux_platform_queue*) queue->platform_queue;

    io_destroy(q->ctxp);
	return APR_SUCCESS;
}


static apr_status_t linux_queue_write(struct async_queue *queue, struct async_queue_entry *ioop)
{
	struct linux_platform_queue *q = (struct linux_platform_queue*) queue->platform_queue;
	struct iocb *event[1];
	struct iocb *iocb;
	int i;

	i = ioop - queue->ioaqes;
	iocb = &(q->iocbs[i]);

    io_prep_pwrite(iocb,((struct linux_platform_file *)queue->workload->worker->file)->fd,
                   ioop->request.buf,
                   ioop->request.size, ioop->request.offset);

#ifdef DEBUG
	printf("Write submitted %d\n",iocb->u.c.nbytes);
#endif


	event[0] = iocb;
	return io_submit(q->ctxp, 1, event) == 1 ? APR_SUCCESS : APR_EGENERAL;
}


static apr_status_t linux_queue_read(struct async_queue *queue, struct async_queue_entry *ioop)
{
	struct linux_platform_queue *q = (struct linux_platform_queue*) queue->platform_queue;
	struct iocb *event[1];
	struct iocb *iocb;
	int i;

	i = ioop - queue->ioaqes;
	iocb = &(q->iocbs[i]);

    io_prep_pread(iocb,
                  ((struct linux_platform_file *)queue->workload->worker->file)->fd,
                   ioop->request.buf,
                   ioop->request.size, ioop->request.offset
                  );

#ifdef DEBUG
	printf("Read sumbitted\n");
#endif

	event[0] = iocb;
	return io_submit(q->ctxp, 1, event) == 1 ? APR_SUCCESS : APR_EGENERAL;
}

static apr_status_t linux_queue_wait(struct async_queue *queue, int block)
{
	struct linux_platform_queue *q = (struct linux_platform_queue*) queue->platform_queue;
	struct io_event *ioe;
	struct async_queue_entry *ioop;
	struct timespec ts;

	int i,j;
	long tmp;


	tmp = io_getevents(q->ctxp, block ? 1 : 0, queue->active, q->events, NULL);
	for(i=0; i < tmp; ++i) {
		ioe = &(q->events)[i];
		j = ioe->obj - q->iocbs;
		ioop = &(queue->ioaqes[j]);

		/* notify */
		generic_queue_notify(queue, ioop);
	}
	return (tmp < 0) ? APR_EGENERAL : APR_SUCCESS;
}

static struct platform_ops linux_platform_ops = {
    &linux_create_io_buffer,
	&linux_get_pagesize,
    &linux_get_min_io_size,
	&linux_file_open,
	&linux_file_truncate,
	&linux_file_close,
	&linux_file_flush,
	&linux_queue_create,
	&linux_queue_destroy,
	&linux_queue_read,
	&linux_queue_write,
	&linux_queue_wait
};

struct platform_ops *platform_ops = &linux_platform_ops;
