//
//  smckeys.c
//  Minimal AppleSMC key reader/writer for YogaSMC verification.
//  Usage:
//    smckeys                - read fan/temperature keys
//    smckeys F0Ac           - read a 4-char key
//    smckeys -w KEY hexbyte - write hex data to a key
//
//  AppleSMC's user client takes an SMCParamStruct through selector 2, not
//  scalar arguments; a read is a READ_KEYINFO call to learn the size and type
//  followed by a READ_BYTES call.
//
#include <IOKit/IOKitLib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static io_connect_t smc;

#define KERNEL_INDEX_SMC     2
#define SMC_CMD_READ_BYTES   5
#define SMC_CMD_WRITE_BYTES  6
#define SMC_CMD_READ_KEYINFO 9

typedef struct {
    unsigned char  major;
    unsigned char  minor;
    unsigned char  build;
    unsigned char  reserved;
    unsigned short release;
} SMCVers_t;

typedef struct {
    uint16_t version;
    uint16_t length;
    uint32_t cpuPLimit;
    uint32_t gpuPLimit;
    uint32_t memPLimit;
} SMCPLimitData_t;

typedef struct {
    uint32_t dataSize;
    uint32_t dataType;
    char     dataAttributes;
} SMCKeyInfoData_t;

typedef struct {
    uint32_t         key;
    SMCVers_t        vers;
    SMCPLimitData_t  pLimitData;
    SMCKeyInfoData_t keyInfo;
    char             result;
    char             status;
    char             data8;
    uint32_t         data32;
    unsigned char    bytes[32];
} SMCParamStruct;

static uint32_t key4(const char *s) {
    return ((uint32_t)(unsigned char)s[0] << 24) | ((uint32_t)(unsigned char)s[1] << 16) |
           ((uint32_t)(unsigned char)s[2] << 8)  |  (uint32_t)(unsigned char)s[3];
}

static void typestr(uint32_t t, char *out) {
    out[0] = (char)(t >> 24); out[1] = (char)(t >> 16);
    out[2] = (char)(t >> 8);  out[3] = (char)t; out[4] = 0;
    for (int i = 0; i < 4; i++)
        if (out[i] < 32 || out[i] > 126) out[i] = ' ';
}

static int smc_call(SMCParamStruct *in, SMCParamStruct *out) {
    size_t outsize = sizeof(SMCParamStruct);
    kern_return_t r = IOConnectCallStructMethod(smc, KERNEL_INDEX_SMC,
                                                in, sizeof(SMCParamStruct),
                                                out, &outsize);
    if (r != kIOReturnSuccess || out->result != 0)
        return -1;
    return 0;
}

// Reads a key into buf (up to *size bytes); returns the SMC type on success.
static int smc_read(const char *key, unsigned char *buf, uint32_t *size, uint32_t *type) {
    SMCParamStruct in = {0}, out = {0};

    in.key   = key4(key);
    in.data8 = SMC_CMD_READ_KEYINFO;
    if (smc_call(&in, &out) != 0)
        return -1;

    uint32_t dsize = out.keyInfo.dataSize;
    if (dsize == 0 || dsize > sizeof(in.bytes))
        return -1;
    if (type) *type = out.keyInfo.dataType;

    in.keyInfo.dataSize = dsize;
    in.data8 = SMC_CMD_READ_BYTES;
    if (smc_call(&in, &out) != 0)
        return -1;

    if (*size > dsize) *size = dsize;
    memcpy(buf, out.bytes, *size);
    *size = dsize;
    return 0;
}

static int smc_write(const char *key, const unsigned char *data, uint32_t len) {
    SMCParamStruct in = {0}, out = {0};
    uint32_t dsize = 0, dtype = 0;
    unsigned char probe[32];

    dsize = sizeof(probe);
    if (smc_read(key, probe, &dsize, &dtype) != 0)
        return -1;
    if (len > dsize) len = dsize;

    in.key = key4(key);
    in.keyInfo.dataSize = dsize;
    in.data8 = SMC_CMD_WRITE_BYTES;
    memcpy(in.bytes, data, len);
    return smc_call(&in, &out);
}

// Renders the value the way its SMC type says it should be read.
static void printval(uint32_t type, const unsigned char *d, uint32_t n) {
    char t[5];
    typestr(type, t);
    if (!strncmp(t, "fpe2", 4) && n >= 2)
        printf("%5u        [fpe2]", (unsigned)(((d[0] << 8) | d[1]) >> 2));
    else if (!strncmp(t, "sp78", 4) && n >= 2)
        printf("%5.2f  C     [sp78]", (double)((signed char)d[0]) + (double)d[1] / 256.0);
    else if (!strncmp(t, "flt ", 4) && n >= 4) {
        float f; memcpy(&f, d, 4);
        printf("%5.2f        [flt]", (double)f);
    } else if (!strncmp(t, "ui8 ", 4) && n >= 1)
        printf("%5u        [ui8]", d[0]);
    else if (!strncmp(t, "ui16", 4) && n >= 2)
        printf("%5u        [ui16]", (unsigned)((d[0] << 8) | d[1]));
    else if (!strncmp(t, "ui32", 4) && n >= 4)
        printf("%5u        [ui32]", (unsigned)((d[0] << 24) | (d[1] << 16) | (d[2] << 8) | d[3]));
    else {
        printf("      ");
        for (uint32_t i = 0; i < n; i++) printf("%02x", d[i]);
        printf("  [%s]", t);
    }
}

static void show(const char *key, const char *label) {
    unsigned char d[32] = {0};
    uint32_t n = sizeof(d), type = 0;
    printf("%-5s %-24s ", key, label);
    if (smc_read(key, d, &n, &type) != 0) {
        printf("unavailable\n");
        return;
    }
    printval(type, d, n);
    printf("\n");
}

int main(int argc, char *argv[]) {
    io_service_t svc = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("AppleSMC"));
    if (!svc) { fprintf(stderr, "AppleSMC not found\n"); return 1; }
    if (IOServiceOpen(svc, mach_task_self_, 0, &smc) != kIOReturnSuccess) {
        fprintf(stderr, "open failed\n");
        return 1;
    }

    int rc = 0;
    if (argc == 2 && strlen(argv[1]) == 4) {
        unsigned char d[32] = {0};
        uint32_t n = sizeof(d), type = 0;
        if (smc_read(argv[1], d, &n, &type) != 0) {
            printf("%s: read failed\n", argv[1]);
            rc = 1;
        } else {
            printf("%-5s %-24s ", argv[1], "");
            printval(type, d, n);
            printf("\n");
        }
    } else if (argc == 4 && !strcmp(argv[1], "-w") && strlen(argv[2]) == 4) {
        unsigned char data[32] = {0};
        long v = strtol(argv[3], NULL, 16);
        data[0] = (unsigned char)v;
        if (smc_write(argv[2], data, 1) != 0) { printf("write failed\n"); rc = 1; }
        else printf("written\n");
    } else {
        show("#KEY", "SMC key count");
        show("FNum", "Fan count");
        show("F0Ac", "Fan 0 RPM");
        show("F0Mn", "Fan 0 min RPM");
        show("F0Mx", "Fan 0 max RPM");
        show("F0Sf", "Fan 0 safe RPM");
        show("F0ID", "Fan 0 ID");
        show("TCXC", "CPU PECI temp");
        show("TM0p", "DIMM0 temp");
        show("TM1p", "DIMM1 temp");
        show("TG0P", "Generic temp 0");
        show("TG1P", "Generic temp 1");
    }

    IOServiceClose(smc);
    return rc;
}
