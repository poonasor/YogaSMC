//
//  smckeys.c
//  Minimal AppleSMC key reader/writer for YogaSMC verification.
//  Usage:
//    smckeys                - read fan/temperature keys
//    smckeys F0Ac           - read a 4-char key
//    smckeys -w KEY hexbyte - write hex data to a key
//
#include <IOKit/IOKitLib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static io_connect_t smc;

#define SMC_READ_KEY 5
#define SMC_WRITE_KEY 6

typedef struct {
    char key[5];
    uint32_t datasize;
    uint16_t datatype[2];
    uint8_t data[32];
} smckey_t;

static int smc_call(int selector, uint32_t key, const uint8_t *in, uint32_t insize, uint8_t *out, uint32_t *outsize) {
    size_t inCount = 2, outCount = 1;
    uint64_t scalarIn[2] = {(uint64_t)key, insize};
    uint64_t scalarOut[1] = {0};
    if (IOConnectCallMethod(smc, selector, scalarIn, inCount, in, insize, scalarOut, &inCount, out, outsize) != kIOReturnSuccess)
        return -1;
    return 0;
}

static int smc_read(const char *key, uint8_t *data, uint32_t size) {
    uint32_t k = (key[0] << 24) | (key[1] << 16) | (key[2] << 8) | key[3];
    uint32_t outsize = size + 32;
    uint8_t buf[64] = {0};
    if (smc_call(SMC_READ_KEY, k, NULL, 0, buf, &outsize) != 0)
        return -1;
    memcpy(data, buf + 4, size);
    return 0;
}

static uint16_t fpe2(const uint8_t *d) {
    return (d[0] << 8) | d[1];
}

static void show(const char *key, const char *label) {
    uint8_t d[2] = {0};
    if (smc_read(key, d, 2) != 0) {
        printf("%-5s unavailable\n", key);
        return;
    }
    printf("%-5s %-22s %5u\n", key, label, fpe2(d));
}

int main(int argc, char *argv[]) {
    io_service_t svc = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("AppleSMC"));
    if (!svc) { fprintf(stderr, "AppleSMC not found\n"); return 1; }
    if (IOServiceOpen(svc, mach_task_self_, 0, &smc) != kIOReturnSuccess) { fprintf(stderr, "open failed\n"); return 1; }

    if (argc == 2) {
        uint8_t d[2] = {0};
        if (smc_read(argv[1], d, 2) != 0) { printf("read failed\n"); return 1; }
        printf("%s: %02x%02x (%u)\n", argv[1], d[0], d[1], fpe2(d));
    } else if (argc == 4 && strcmp(argv[1], "-w") == 0) {
        uint32_t k = (argv[2][0] << 24) | (argv[2][1] << 16) | (argv[2][2] << 8) | argv[2][3];
        uint8_t data[32] = {0};
        long v = strtol(argv[3], NULL, 16);
        data[0] = (uint8_t)v;
        uint32_t outsize = 32;
        if (smc_call(SMC_WRITE_KEY, k, data, 1, NULL, &outsize) != 0) { printf("write failed\n"); return 1; }
        printf("written\n");
    } else {
        uint8_t d[1] = {0};
        if (smc_read("FNum", d, 1) == 0)
            printf("FNum  %-22s %5u\n", "Fan count", d[0]);
        else
            printf("FNum  unavailable (no fan plugin)\n");
        show("F0Ac", "Fan 0 RPM");
        show("F0Mn", "Fan 0 min RPM");
        show("F0Mx", "Fan 0 max RPM");
        show("F0Sf", "Fan 0 safe RPM");
        show("TCXC", "CPU PECI temp (sp78 C)");
        show("TM0p", "DIMM0 temp");
        show("TM1p", "DIMM1 temp");
        show("TG0P", "Generic temp 0");
    }
    IOServiceClose(smc);
    return 0;
}
