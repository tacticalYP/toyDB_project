#include "pf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "spage.h"
#include "pftypes.h" /* for PF_PAGE_SIZE */

#define SP_HEADER_SIZE (sizeof(SPageHeader))
#define SP_SLOT_SIZE   (sizeof(SlotEntry))

/* Internal helpers */

static inline SPageHeader* hdr_from_buf(char *pagebuf) {
    return (SPageHeader*) pagebuf;
}

static inline SlotEntry* slots_from_buf(char *pagebuf) {
    return (SlotEntry*) (pagebuf + SP_HEADER_SIZE);
}

/* compute total bytes used by records in a page (sum length of used slots) */
static int compute_used_record_bytes(char *pagebuf) {
    SPageHeader *hdr = hdr_from_buf(pagebuf);
    SlotEntry *slots = slots_from_buf(pagebuf);
    int sum = 0;
    for (int i = 0; i < hdr->slotCount; ++i) {
        if (slots[i].used)
            sum += slots[i].length;
    }
    return sum;
}

/* Initialize a freshly allocated page (call after PF_AllocPage) */
int SP_InitPage(char *pagebuf) {
    if (!pagebuf) return -1;
    SPageHeader *hdr = hdr_from_buf(pagebuf);
    hdr->slotCount = 0;
    hdr->freeOffset = SP_HEADER_SIZE; /* slot array grows after this */
    hdr->freeSpace = PF_PAGE_SIZE - SP_HEADER_SIZE;
    hdr->reserved = 0;
    /* zero slot area for clarity (not strictly necessary) */
    memset(pagebuf + SP_HEADER_SIZE, 0, PF_PAGE_SIZE - SP_HEADER_SIZE);
    return 0;
}

/* Try to find a page with enough free space, scanning all used pages.
   If none found, returns PFE_EOF. On success returns PFE_OK and leaves
   the page pinned (i.e., do NOT unfix). Caller must unfix or write changes. */
static int find_page_with_space(int fd, int reqBytes, int *outPage, char **outPageBuf) {
    int pagenum;
    char *pagebuf;
    int rc;

    pagenum = -1;
    /* iterate through existing pages */
    rc = PF_GetNextPage(fd, &pagenum, &pagebuf);
    while (rc == PFE_OK) {
        SPageHeader *hdr = hdr_from_buf(pagebuf);
        /* if header not initialized (slotCount insane), treat as empty used page */
        if (hdr->slotCount == 0 && hdr->freeSpace == 0) {
            /* Page probably newly allocated but not initialized; treat as insufficient */
        } else {
            if (hdr->freeSpace >= reqBytes) {
                /* page fits */
                *outPage = pagenum;
                *outPageBuf = pagebuf;
                return PFE_OK; /* page is pinned by PF_GetNextPage */
            }
        }
        /* not chosen -> unfix and continue */
        if ((rc = PF_UnfixPage(fd, pagenum, FALSE)) != PFE_OK) return rc;
        rc = PF_GetNextPage(fd, &pagenum, &pagebuf);
    }
    return PFE_EOF;
}

/* Insert record: finds a page or allocates new one */
int SP_InsertRecord(int fd, const void *rec, int len, RecordID *rid) {
    if (!rec || len <= 0 || !rid) return -1;
    int reqBytes = len + SP_SLOT_SIZE;
    int pagenum;
    char *pagebuf;
    int rc;

    /* Try to find an existing page with space */
    rc = find_page_with_space(fd, reqBytes, &pagenum, &pagebuf);
    if (rc == PFE_EOF) {
        /* allocate new page */
        rc = PF_AllocPage(fd, &pagenum, &pagebuf);
        if (rc != PFE_OK) return rc;
        SP_InitPage(pagebuf);
        /* page is pinned and ready for write */
    } else if (rc != PFE_OK) {
        return rc;
    }
    /* At this point pagebuf is pinned and points to chosen page */
    SPageHeader *hdr = hdr_from_buf(pagebuf);
    SlotEntry *slots = slots_from_buf(pagebuf);

    /* ensure header fields meaningful: if page looks uninitialized, init it */
    if (hdr->slotCount == 0 && hdr->freeSpace == 0) {
        /* defensive init (if PF_GetNextPage returned a used page with no hdr init) */
        hdr->slotCount = 0;
        hdr->freeOffset = SP_HEADER_SIZE;
        hdr->freeSpace = PF_PAGE_SIZE - SP_HEADER_SIZE;
    }

    /* compute used record bytes in the page */
    int usedRecBytes = compute_used_record_bytes(pagebuf);
    int recordPos = PF_PAGE_SIZE - usedRecBytes - len;
    /* check slot placement */
    int slotIndex = hdr->slotCount; /* append */
    int required_total = SP_HEADER_SIZE + (slotIndex + 1) * SP_SLOT_SIZE + (PF_PAGE_SIZE - recordPos);
    /* or simply check hdr->freeSpace */
    if (hdr->freeSpace < reqBytes) {
        /* this should not happen because we checked before; unfix and return error */
        PF_UnfixPage(fd, pagenum, FALSE);
        return PFE_NOBUF;
    }

    /* Write record bytes */
    memcpy(pagebuf + recordPos, rec, len);

    /* Fill slot */
    slots[slotIndex].offset = (uint16_t) recordPos;
    slots[slotIndex].length = (uint16_t) len;
    slots[slotIndex].used = 1;
    slots[slotIndex].pad = 0;

    /* Update header */
    hdr->slotCount = slotIndex + 1;
    hdr->freeSpace = hdr->freeSpace - reqBytes;

    /* Fill RecordID */
    rid->pageNum = pagenum;
    rid->slotNum = slotIndex;

    /* mark dirty and unfix */
    rc = PF_UnfixPage(fd, pagenum, TRUE);
    if (rc != PFE_OK) return rc;
    return PFE_OK;
}

/* Get record: allocates a buffer and returns it (caller must free) */
int SP_GetRecord(int fd, RecordID rid, void **outbuf, int *outlen) {
    if (!outbuf || !outlen) return -1;
    char *pagebuf;
    int rc = PF_GetThisPage(fd, rid.pageNum, &pagebuf);
    if (rc != PFE_OK) return rc;
    SPageHeader *hdr = hdr_from_buf(pagebuf);
    if (rid.slotNum < 0 || rid.slotNum >= hdr->slotCount) {
        PF_UnfixPage(fd, rid.pageNum, FALSE);
        return PFE_INVALIDPAGE;
    }
    SlotEntry *slots = slots_from_buf(pagebuf);
    SlotEntry s = slots[rid.slotNum];
    if (!s.used) {
        PF_UnfixPage(fd, rid.pageNum, FALSE);
        return PFE_PAGEFREE; /* reuse an error code meaning deleted */
    }
    /* copy to newly allocated buffer */
    void *buf = malloc(s.length);
    if (!buf) {
        PF_UnfixPage(fd, rid.pageNum, FALSE);
        return PFE_NOMEM;
    }
    memcpy(buf, pagebuf + s.offset, s.length);
    *outbuf = buf;
    *outlen = s.length;
    PF_UnfixPage(fd, rid.pageNum, FALSE);
    return PFE_OK;
}

/* Delete record (lazy): mark slot unused and mark page dirty */
int SP_DeleteRecord(int fd, RecordID rid) {
    char *pagebuf;
    int rc = PF_GetThisPage(fd, rid.pageNum, &pagebuf);
    if (rc != PFE_OK) return rc;
    SPageHeader *hdr = hdr_from_buf(pagebuf);
    if (rid.slotNum < 0 || rid.slotNum >= hdr->slotCount) {
        PF_UnfixPage(fd, rid.pageNum, FALSE);
        return PFE_INVALIDPAGE;
    }
    SlotEntry *slots = slots_from_buf(pagebuf);
    if (!slots[rid.slotNum].used) {
        PF_UnfixPage(fd, rid.pageNum, FALSE);
        return PFE_PAGEFREE;
    }
    slots[rid.slotNum].used = 0;
    /* increase freeSpace by record len + slot size */
    hdr->freeSpace = hdr->freeSpace + slots[rid.slotNum].length + SP_SLOT_SIZE;
    PF_UnfixPage(fd, rid.pageNum, TRUE);
    return PFE_OK;
}

/* Scan implementation: simple fixed-size array of handles */
typedef struct {
    int in_use;
    int fd;
    int curPage;
    int curSlot;
    int lastPagePinned; /* 1 if curPage is pinned */
    char *pagebuf;      /* pointer from PF_GetThisPage / GetNextPage when pinned */
} SP_ScanState;

static SP_ScanState sp_scans[SP_MAX_SCANS];

int SP_OpenScan(int fd, SP_ScanHandle *sh) {
    if (!sh) return -1;
    int si = -1;
    for (int i = 0; i < SP_MAX_SCANS; ++i) if (!sp_scans[i].in_use) { si = i; break; }
    if (si < 0) return -1;
    sp_scans[si].in_use = 1;
    sp_scans[si].fd = fd;
    sp_scans[si].curPage = -1; /* start before first */
    sp_scans[si].curSlot = 0;
    sp_scans[si].lastPagePinned = 0;
    sp_scans[si].pagebuf = NULL;
    *sh = si;
    return PFE_OK;
}

/* Get next record: returns malloc'd buffer (caller frees) */
int SP_GetNext(int sh, void **outbuf, int *outlen, RecordID *rid) {
    if (sh < 0 || sh >= SP_MAX_SCANS) return -1;
    if (!sp_scans[sh].in_use) return -1;
    int fd = sp_scans[sh].fd;
    int pnum = sp_scans[sh].curPage;
    char *pagebuf = NULL;
    int rc;

    /* iterate pages */
    while (1) {
        if (pnum == -1) {
            /* get first page */
            rc = PF_GetFirstPage(fd, &pnum, &pagebuf);
            if (rc == PFE_EOF) return PFE_EOF;
            if (rc != PFE_OK) return rc;
            sp_scans[sh].lastPagePinned = 1;
            sp_scans[sh].pagebuf = pagebuf;
            sp_scans[sh].curPage = pnum;
            sp_scans[sh].curSlot = 0;
        } else {
            /* pnum is current page; if curSlot < slotCount search; else move to next page */
        }

        pagebuf = sp_scans[sh].pagebuf;
        SPageHeader *hdr = hdr_from_buf(pagebuf);
        SlotEntry *slots = slots_from_buf(pagebuf);

        while (sp_scans[sh].curSlot < hdr->slotCount) {
            int sidx = sp_scans[sh].curSlot++;
            if (!slots[sidx].used) continue;
            /* produce record */
            void *buf = malloc(slots[sidx].length);
            if (!buf) {
                PF_UnfixPage(fd, sp_scans[sh].curPage, FALSE);
                sp_scans[sh].in_use = 0;
                return PFE_NOMEM;
            }
            memcpy(buf, pagebuf + slots[sidx].offset, slots[sidx].length);
            *outbuf = buf;
            *outlen = slots[sidx].length;
            if (rid) { rid->pageNum = sp_scans[sh].curPage; rid->slotNum = sidx; }
            return PFE_OK;
        }

        /* finished slots in this page: unfix and go to next page */
        if (sp_scans[sh].lastPagePinned) {
            PF_UnfixPage(fd, sp_scans[sh].curPage, FALSE);
            sp_scans[sh].lastPagePinned = 0;
            sp_scans[sh].pagebuf = NULL;
        }
        /* move to next page */
        int nextp = sp_scans[sh].curPage;
        rc = PF_GetNextPage(fd, &nextp, &pagebuf);
        if (rc == PFE_EOF) return PFE_EOF;
        if (rc != PFE_OK) return rc;
        sp_scans[sh].curPage = nextp;
        sp_scans[sh].curSlot = 0;
        sp_scans[sh].lastPagePinned = 1;
        sp_scans[sh].pagebuf = pagebuf;
        pnum = nextp;
    }
}

/* Close scan: unfix pinned page and mark handle free */
int SP_CloseScan(int sh) {
    if (sh < 0 || sh >= SP_MAX_SCANS) return -1;
    if (!sp_scans[sh].in_use) return -1;
    if (sp_scans[sh].lastPagePinned) {
        PF_UnfixPage(sp_scans[sh].fd, sp_scans[sh].curPage, FALSE);
        sp_scans[sh].lastPagePinned = 0;
        sp_scans[sh].pagebuf = NULL;
    }
    sp_scans[sh].in_use = 0;
    return PFE_OK;
}

/* page utilization calculation */
int SP_PageUtilization(const char *pagebuf, float *util_percent, int *used_bytes) {
    if (!pagebuf || !util_percent || !used_bytes) return -1;
    const SPageHeader *hdr = (const SPageHeader*) pagebuf;
    const SlotEntry *slots = (const SlotEntry*) (pagebuf + SP_HEADER_SIZE);
    int used_slots_bytes = hdr->slotCount * SP_SLOT_SIZE;
    int total_record_bytes = 0;
    for (int i = 0; i < hdr->slotCount; ++i) {
        if (slots[i].used) total_record_bytes += slots[i].length;
    }
    int used = SP_HEADER_SIZE + used_slots_bytes + total_record_bytes;
    *used_bytes = used;
    *util_percent = ((float)used / (float)PF_PAGE_SIZE) * 100.0f;
    return PFE_OK;
}
