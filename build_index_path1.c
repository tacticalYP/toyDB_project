/* build_index_path1.c
 * Bulk-build an AM index from an existing slotted DB produced by test_spage
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pflayer/spage.h"
#include "pflayer/pf.h"
#include "amlayer/testam.h" /* provides INT_TYPE etc. */

/* Declare the AM API functions we call (no header with prototypes is safe to include) */
int AM_CreateIndex(char *fileName, int indexNo, char attrType, int attrLength);
int AM_InsertEntry(int fileDesc, char attrType, int attrLength, char *value, int recId);
int AM_DestroyIndex(char *fileName, int indexNo);

#define SLOTTED_DB "pflayer/slotted_student.db"
#define IDX_NAME   "student"

typedef struct { int key; int recid; } KeyRec;

static int cmp_kr(const void *a, const void *b) {
    const KeyRec *A = a; const KeyRec *B = b;
    if (A->key < B->key) return -1;
    if (A->key > B->key) return 1;
    return 0;
}

/* crude parser: extract first integer (roll-no) from a record buffer */
static int parse_rollno(const char *rec, int len) {
    int i = 0;
    while (i < len && (rec[i] == ' ' || rec[i] == '\t' || rec[i] == '\n' || rec[i] == '\r')) i++;
    int sign = 1;
    if (i < len && rec[i] == '-') { sign = -1; i++; }
    int val = 0; int found = 0;
    while (i < len && rec[i] >= '0' && rec[i] <= '9') {
        found = 1; val = val*10 + (rec[i]-'0'); i++; }
    return found ? val*sign : -1;
}

int main(void) {
    PF_Init();

    int sdfd = PF_OpenFile(SLOTTED_DB, "LRU");
    if (sdfd < 0) {
        fprintf(stderr, "ERROR: cannot open slotted DB '%s'\n", SLOTTED_DB);
        return 1;
    }

    SP_ScanHandle sh;
    if (SP_OpenScan(sdfd, &sh) != PFE_OK) {
        fprintf(stderr, "ERROR: SP_OpenScan failed\n");
        PF_CloseFile(sdfd);
        return 1;
    }

    KeyRec *arr = NULL; size_t arr_sz = 0, arr_cap = 0;
    void *recbuf = NULL; int reclen = 0; RecordID rid;
    int scanrc;
    while ((scanrc = SP_GetNext(sh, &recbuf, &reclen, &rid)) == PFE_OK) {
        int key = parse_rollno((const char*)recbuf, reclen);
        if (arr_sz == arr_cap) {
            arr_cap = arr_cap ? arr_cap*2 : 1024;
            arr = realloc(arr, arr_cap * sizeof(KeyRec));
            if (!arr) { perror("realloc"); exit(1); }
        }
        /* pack pageNum+slotNum into a 32-bit recid: high 16 bits page, low 16 bits slot */
        int recid = ((rid.pageNum & 0xFFFF) << 16) | (rid.slotNum & 0xFFFF);
        arr[arr_sz].key = key;
        arr[arr_sz].recid = recid;
        arr_sz++;
        free(recbuf); recbuf = NULL;
    }

    if (scanrc != PFE_EOF && scanrc != PFE_OK) {
        fprintf(stderr, "Error scanning slotted DB (rc=%d)\n", scanrc);
        SP_CloseScan(sh);
        PF_CloseFile(sdfd);
        free(arr);
        return 1;
    }

    SP_CloseScan(sh);
    PF_CloseFile(sdfd);

    if (arr_sz == 0) {
        fprintf(stderr, "No records found in slotted DB '%s'\n", SLOTTED_DB);
        free(arr);
        return 1;
    }

    qsort(arr, arr_sz, sizeof(KeyRec), cmp_kr);

    /* create AM index */
    AM_CreateIndex(IDX_NAME, 0, INT_TYPE, sizeof(int));
    char idxfname[256]; snprintf(idxfname, sizeof(idxfname), "%s.0", IDX_NAME);
    int idxfd = PF_OpenFile(idxfname, "LRU");
    if (idxfd < 0) {
        fprintf(stderr, "ERROR: cannot open AM index file '%s'\n", idxfname);
        free(arr);
        return 1;
    }

    for (size_t i = 0; i < arr_sz; i++) {
        int k = arr[i].key;
        int ridint = arr[i].recid;
        if (AM_InsertEntry(idxfd, INT_TYPE, sizeof(int), (char *)&k, ridint) < 0) {
            fprintf(stderr, "AM_InsertEntry failed for key %d\n", k);
        }
    }

    printf("Inserted %zu records into index '%s' (bulk sorted)\n", arr_sz, IDX_NAME);

    PF_CloseFile(idxfd);
    free(arr);
    return 0;
}
