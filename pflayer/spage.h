#ifndef SPAGE_H
#define SPAGE_H

#include "pf.h"
#include <stdint.h>

/* On-page header */
typedef struct {
    uint16_t freeOffset;   /* offset from start where free area begins (unused) */
    uint16_t slotCount;    /* number of slots allocated */
    uint16_t freeSpace;    /* convenience: total free bytes available */
    uint16_t reserved;     /* padding */
} SPageHeader;             /* sizeof = 8 bytes (with uint16_t) */

/* Slot directory entry */
typedef struct {
    uint16_t offset;       /* offset from page start to record */
    uint16_t length;       /* length of record */
    uint8_t  used;         /* 1 = used, 0 = free */
    uint8_t  pad;          /* padding to make struct size = 6 -> 8 (we'll use 6) */
} SlotEntry;

/* Record identifier returned to caller */
typedef struct {
    int pageNum;
    int slotNum;
} RecordID;

/* Scan handle (opaque to caller) */
typedef int SP_ScanHandle;

/* API */
int SP_InitPage(char *pagebuf);

int SP_InsertRecord(int fd, const void *rec, int len, RecordID *rid);
/* Allocates and returns a malloc'd buffer pointed by *outbuf; caller must free */
int SP_GetRecord(int fd, RecordID rid, void **outbuf, int *outlen);

int SP_DeleteRecord(int fd, RecordID rid);

/* Sequential scan */
int SP_OpenScan(int fd, SP_ScanHandle *sh);
int SP_GetNext(int sh, void **outbuf, int *outlen, RecordID *rid);
int SP_CloseScan(int sh);

/* Page utilization: compute used bytes and percent */
int SP_PageUtilization(const char *pagebuf, float *util_percent, int *used_bytes);

/* Helpful constant */
#define SP_MAX_SCANS 16

#endif /* SPAGE_H */
