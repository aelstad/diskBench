/*
  * win32.c
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
#define WINVER 0x0501
#include "diskBench.h"
#include <winsock2.h>
#include <windows.h>
#include <winioctl.h>


struct win32_platform_file {
	HANDLE hFile;
	HANDLE completionPort;
};

struct win32_async_queue {
	struct async_queue *queue;

	OVERLAPPED *overlapped;
};

static apr_size_t win32_get_page_size()
{
	return 4096;
}


static apr_size_t win32_get_min_iosize()
{
	return 512;
}

static apr_status_t win32_create_io_buffer(void **buf, uint64_t size)
{
    if(size % win32_get_page_size() != 0)
        // round up to nearest page-size
        size = (size/win32_get_page_size() + 1)*win32_get_page_size();

    *buf = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

    return (*buf != NULL) ? APR_SUCCESS : APR_ENOMEM;
}

static apr_status_t win32_file_truncate(struct platform_file *the_file, uint64_t *length)
{
    LARGE_INTEGER distance;
    LARGE_INTEGER end;

    struct win32_platform_file *file = (struct win32_platform_file *) the_file;

    distance.QuadPart = *length;
    SetFilePointerEx(file->hFile, distance, &end, FILE_BEGIN);
    SetEndOfFile(file->hFile);

    distance.QuadPart = 0;
    SetFilePointerEx(file->hFile, distance, &end, FILE_END);
    *length = end.QuadPart;


    return APR_SUCCESS;
}



static apr_status_t win32_file_open(char *filename, uint64_t *length, double freespace_percentage,
                                    int *file_truncated, struct platform_file **rv)
{
	struct win32_platform_file *file;
	HANDLE hFile;
	LONG offlo;
    LONG offhi;
    LARGE_INTEGER distance;
    LARGE_INTEGER end;
    apr_status_t status;
    BY_HANDLE_FILE_INFORMATION fileInformation;
    apr_uint64_t current_length;
    char volumepathname[100];

	*rv = NULL;

    /* file pre-allocation */
 	hFile = CreateFile(filename,       // file to open
                   GENERIC_READ|GENERIC_WRITE, // open for reading
                   FILE_SHARE_READ|FILE_SHARE_WRITE, 		 // share read/write
                   NULL,                  // default security
                   OPEN_EXISTING,
                   FILE_FLAG_NO_BUFFERING|FILE_FLAG_OVERLAPPED, // normal file
                   NULL);                 // no attr. template

	if(hFile == INVALID_HANDLE_VALUE) {
        hFile = CreateFile(filename,       // file to open
                   GENERIC_READ|GENERIC_WRITE, // open for reading
                   FILE_SHARE_READ|FILE_SHARE_WRITE, 		   // share read/write
                   NULL,                  // default security
                   CREATE_ALWAYS,
                   FILE_FLAG_NO_BUFFERING|FILE_FLAG_OVERLAPPED, // normal file
                   NULL);                 // no attr. template
	}
	if(hFile == INVALID_HANDLE_VALUE)
        return APR_EGENERAL;

    /* Detect if file by looking at filename */
    int filenamelen = strlen(filename);
    int isDevice = (*(filename + filenamelen - 1) == ':');
    int isFile = !isDevice;

	file = malloc(sizeof(struct win32_platform_file));
	file->hFile = hFile;

    file->completionPort = CreateIoCompletionPort(
              INVALID_HANDLE_VALUE,
              NULL,
              0,
              0);

    if(file->completionPort == NULL) {
        printf("Could not create completion port\n");
        return APR_EGENERAL;
    }

    /* Assign completion port to existing file */
    if(CreateIoCompletionPort(file->hFile, file->completionPort, 0, 0) == NULL) {
        printf("Could not assign completion port\n");
        return APR_EGENERAL;
    }

    if(isFile) {
        /* Fetch current length */
        distance.QuadPart = 0;
        SetFilePointerEx(hFile, distance, &end, FILE_END);
        current_length = end.QuadPart;



        /* Fetch free space */
        ULARGE_INTEGER bytesAvailable;
        ULARGE_INTEGER totalBytes;
        ULARGE_INTEGER freeBytes;
        /* Set length based on max of size and freespace */
        GetVolumePathName(filename, volumepathname,100);
        GetDiskFreeSpaceEx(volumepathname, &bytesAvailable, &totalBytes, &freeBytes);

        /* override size if requested is larger than space available */
        if(*length > freeBytes.QuadPart+current_length) {
            *length = (freeBytes.QuadPart + current_length)*freespace_percentage;
            /* round off */
            *length = *length - *length % win32_get_page_size();
        }
        /* reuse current_length if it exists */
        if(current_length > 128*APR_UINT64_C(1048576)) {
            *length = current_length;
        }

        if(*length == 0) {
            /* calculate size based on space available*/
            *length = (apr_uint64_t) (((double) freeBytes.QuadPart+current_length)*freespace_percentage);

            /* round off to 128MB boundary */
            *length /= 128*APR_UINT64_C(1048576);
            *length *= 128*APR_UINT64_C(1048576);
        }
        *file_truncated = *length != current_length;
    } else {
        OVERLAPPED* pov = NULL;
        ULONG_PTR key = 0;

        OVERLAPPED overlapped;
        overlapped.hEvent = 0;

        GET_LENGTH_INFORMATION info;
        DWORD infosize;

        if(DeviceIoControl(hFile,IOCTL_DISK_GET_LENGTH_INFO, NULL, 0,  &info, sizeof(GET_LENGTH_INFORMATION), &infosize, &overlapped)==0)
        {
            printf("Failed to get size\n");
            return APR_EGENERAL;
        }

        if(GetQueuedCompletionStatus(file->completionPort, &infosize, &key, &pov, INFINITE)==0)
        {
            printf("Failed to get completion port size\n");
            return APR_EGENERAL;
        }

        current_length = info.Length.QuadPart;
        *file_truncated = 0;
    }


	*rv = (struct platform_file*) file;

    if(*file_truncated) {
        status = win32_file_truncate(*rv, length);
        if(status != APR_SUCCESS)
            return status;
    } else {
        *length = current_length;
    }

	return APR_SUCCESS;
}


static apr_status_t win32_file_close(struct platform_file *the_file)
{
	struct win32_platform_file *file = (struct win32_platform_file *) the_file;

	if(CloseHandle(file->hFile)==0)
		return APR_EGENERAL;

    if(CloseHandle(file->completionPort) == 0)
        return APR_EGENERAL;

	free(file);

	return APR_SUCCESS;
}

static apr_status_t win32_file_flush(struct platform_file *the_file)
{
	struct win32_platform_file *file = (struct win32_platform_file *) the_file;

	if(FlushFileBuffers(file->hFile)==0)
		return APR_EGENERAL;

	return APR_SUCCESS;
}


static apr_status_t win32_queue_create(struct async_queue *queue)
{
	int i;
	struct win32_async_queue *q;
	q = apr_pcalloc(queue->pool, sizeof(struct win32_async_queue));

    q->overlapped = apr_pcalloc(queue->pool, sizeof(OVERLAPPED)*queue->total);
	queue->platform_queue = (struct async_platform_queue*) q;

	return APR_SUCCESS;
}

static apr_status_t win32_queue_destroy(struct async_queue *queue)
{
	struct win32_async_queue *q = (struct win32_async_queue*) queue->platform_queue;

	return APR_SUCCESS;
}

static apr_status_t win32_queue_read(struct async_queue *queue, struct async_queue_entry *ioop)
{
	int i;
	struct win32_async_queue *q = (struct win32_async_queue*) queue->platform_queue;
	struct win32_platform_file *file = (struct win32_platform_file*) queue->workload->worker->file;
	OVERLAPPED* overlapped;

	i = ioop - queue->ioaqes;
	overlapped = &(q->overlapped[i]);
	overlapped->hEvent = 0;
	overlapped->Offset = (DWORD) ioop->request.offset;
	overlapped->OffsetHigh = (DWORD) (ioop->request.offset>>32);
	return ReadFile(file->hFile, ioop->request.buf, ioop->request.size, NULL, overlapped)!=0 || GetLastError()==ERROR_IO_PENDING ? APR_SUCCESS : APR_EGENERAL;
}

static apr_status_t win32_queue_write(struct async_queue *queue, struct async_queue_entry *ioop)
{
	int i;
	struct win32_async_queue *q = (struct win32_async_queue*) queue->platform_queue;
	struct win32_platform_file *file = (struct win32_platform_file*) queue->workload->worker->file;
	OVERLAPPED* overlapped;
	i = ioop - queue->ioaqes;
	overlapped = &(q->overlapped[i]);

	overlapped->hEvent = 0;
	overlapped->Offset = (DWORD) ioop->request.offset;
	overlapped->OffsetHigh = (DWORD) (ioop->request.offset>>32);
	return WriteFile(file->hFile, ioop->request.buf, ioop->request.size, NULL, overlapped)!=0 || GetLastError()==ERROR_IO_PENDING ? APR_SUCCESS : APR_EGENERAL;
}

static apr_status_t win32_queue_wait(struct async_queue *queue, int block)
{
    DWORD bytesTransfered;
    OVERLAPPED* pov = NULL;
    ULONG_PTR key = 0;

	DWORD rv;
	struct win32_async_queue *q = (struct win32_async_queue*) queue->platform_queue;
    struct win32_platform_file *file = (struct win32_platform_file*) queue->workload->worker->file;

	rv = GetQueuedCompletionStatus(file->completionPort, &bytesTransfered, &key, &pov, block ? INFINITE : 0);
    if(!rv && !block && pov == NULL) {
        return APR_SUCCESS;
    }

    if(rv && pov != NULL) {
        int i;
        struct async_queue_entry *ioop;
        i = pov - q->overlapped;
        ioop = &(queue->ioaqes[i]);
        return generic_queue_notify(queue, ioop);
    }
	return APR_EGENERAL;
}


struct platform_ops win32_platform_ops = {
    &win32_create_io_buffer,
    &win32_get_page_size,
	&win32_get_min_iosize,
	&win32_file_open,
	&win32_file_truncate,
	&win32_file_close,
	&win32_file_flush,
	&win32_queue_create,
	&win32_queue_destroy,
	&win32_queue_read,
	&win32_queue_write,
	&win32_queue_wait
};


struct platform_ops *platform_ops = &win32_platform_ops;

