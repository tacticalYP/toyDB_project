/* build_index_path2.c
 * Incrementally build an AM index by reading student.txt
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "amlayer/testam.h" /* for INT_TYPE */
#include "pflayer/pf.h"

/* AM function prototypes used by this driver */
int AM_CreateIndex(char *fileName, int indexNo, char attrType, int attrLength);
int AM_InsertEntry(int fileDesc, char attrType, int attrLength, char *value, int recId);
int AM_DestroyIndex(char *fileName, int indexNo);

#define INPUT_FILE "pflayer/student.txt"
#define IDX_NAME   "student"

static int parse_rollno_from_line(const char *line) {
    const char *p = line;
    while (*p && (*p==' '||*p=='\t')) p++;
    int sign = 1;
    if (*p=='-') { sign = -1; p++; }
    int val = 0; int found = 0;
    while (*p >= '0' && *p <= '9') { found = 1; val = val*10 + (*p - '0'); p++; }
    return found ? val*sign : -1;
}

int main(void) {
    PF_Init();

    FILE *fp = fopen(INPUT_FILE, "r");
    if (!fp) {
        fprintf(stderr, "Cannot open %s\n", INPUT_FILE);
        return 1;
    }

    AM_CreateIndex(IDX_NAME, 1, INT_TYPE, sizeof(int));
    char idxfname[256]; snprintf(idxfname, sizeof(idxfname), "%s.0", IDX_NAME);
    int idxfd = PF_OpenFile(idxfname, "LRU");
    if (idxfd < 0) {
        fprintf(stderr, "ERROR: cannot open AM index file '%s'\n", idxfname);
        fclose(fp);
        return 1;
    }

    char line[8192];
    int recid = 0; int inserted = 0;
    while (fgets(line, sizeof(line), fp)) {
        int key = parse_rollno_from_line(line);
        if (key < 0) continue;
        if (AM_InsertEntry(idxfd, INT_TYPE, sizeof(int), (char *)&key, recid) < 0) {
            fprintf(stderr, "AM_InsertEntry failed for key %d\n", key);
        } else inserted++;
        recid++;
    }

    printf("Incrementally inserted %d records into index '%s'\n", inserted, IDX_NAME);

    PF_CloseFile(idxfd);
    fclose(fp);
    return 0;
}
