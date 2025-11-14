#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pf.h"
#include "pftypes.h"
#include "spage.h"

#define INPUT_FILE     "student.txt"
#define SLOTTED_DB     "slotted_student.db"

#define MAX_RECORDS 20000
char *records[MAX_RECORDS];
int recordCount = 0;

/* -------------------------------------------------------
   LOAD DATA FILE (student.txt)
   ------------------------------------------------------- */
void load_data_file() {
    FILE *fp = fopen(INPUT_FILE, "r");
    if (!fp) {
        printf("ERROR: Could not open %s\n", INPUT_FILE);
        exit(1);
    }

    char line[5000];
    while (fgets(line, sizeof(line), fp)) {
        int len = strlen(line);
        if (len <= 1) continue;

        records[recordCount] = (char *) malloc(len + 1);
        strcpy(records[recordCount], line);
        recordCount++;
    }

    fclose(fp);

    printf("Loaded %d records from %s\n", recordCount, INPUT_FILE);
}

/* -------------------------------------------------------
   STATIC STORAGE COMPARISON
   ------------------------------------------------------- */
void compute_static_utilization(int fixedSize) {
    int usablePerPage = PF_PAGE_SIZE / fixedSize;
    int pagesNeeded = (recordCount + usablePerPage - 1) / usablePerPage;

    long totalData = (long) recordCount * fixedSize;
    long totalSpace = (long) pagesNeeded * PF_PAGE_SIZE;
    float util = ((float) totalData / totalSpace) * 100.0f;

    printf("Static (%3d bytes) : pages = %d, util = %.2f%%\n",
           fixedSize, pagesNeeded, util);
}

/* -------------------------------------------------------
   SLOTTED STORAGE
   ------------------------------------------------------- */
void compute_slotted_storage() {
    int fd;

    if (PF_CreateFile(SLOTTED_DB) != PFE_OK) {
        PF_PrintError("PF_CreateFile");
        exit(1);
    }

    fd = PF_OpenFile(SLOTTED_DB, "LRU");
    if (fd < 0) {
        PF_PrintError("PF_OpenFile");
        exit(1);
    }

    RecordID rid;
    for (int i = 0; i < recordCount; i++) {
        int len = strlen(records[i]) + 1;

        int rc = SP_InsertRecord(fd, records[i], len, &rid);
        if (rc != PFE_OK) {
            printf("Insert failed at record %d\n", i);
            PF_PrintError("SP_InsertRecord");
            exit(1);
        }
    }

    /* Now compute slotted page utilization */
    int pagenum = -1;
    char *pagebuf;
    int rc;

    int totalPages = 0;
    int totalUsed = 0;

    printf("\n=== SLOTTED PAGE UTILIZATION ===\n");

    while ((rc = PF_GetNextPage(fd, &pagenum, &pagebuf)) == PFE_OK) {

        float util;
        int usedBytes;
        SP_PageUtilization(pagebuf, &util, &usedBytes);

        printf(" Page %d: %d bytes (%.2f%%)\n", pagenum, usedBytes, util);

        totalPages++;
        totalUsed += usedBytes;

        PF_UnfixPage(fd, pagenum, FALSE);
    }

    if (rc != PFE_EOF) {
        PF_PrintError("PF_GetNextPage");
        exit(1);
    }

    float avgUtil = (float) totalUsed / (totalPages * PF_PAGE_SIZE) * 100.0;
    printf("-----------------------------------------\n");
    printf(" Slotted: pages = %d, avg util = %.2f%%\n",
           totalPages, avgUtil);

    PF_CloseFile(fd);
}

/* -------------------------------------------------------
   MAIN: RUN BOTH METHODS
   ------------------------------------------------------- */
int main() {

    printf("=== COMPARISON: STATIC vs SLOTTED PAGE STORAGE ===\n\n");

    /* 1. Load text file */
    load_data_file();

    /* 2. STATIC STORAGE */
    printf("\n=== STATIC STORAGE RESULTS ===\n");
    compute_static_utilization(64);
    compute_static_utilization(128);
    compute_static_utilization(256);

    /* 3. SLOTTED STORAGE */
    compute_slotted_storage();

    printf("\n=== DONE ===\n");
    return 0;
}
