#pragma once

#include <stdint.h>

struct MasterRecord {
    // Validations1
    char sig[7] = {'U','N','I','F','I','E','D'};
    uint8_t rev = 1;

    // Rounding
    uint8_t Reserved0 = 0;
    uint16_t Reserved1 = 0;
    uint32_t Reserved2 = 0;

    // ClusterMapping
    uint64_t ClusterMapOffset = 1; // 1 Sector is default
    uint64_t ClusterMapSize = 0;
    uint64_t ClusterSize = 16; // Max 65536
    
    // Root Location
    uint16_t RootSectorIndex = 0;
    uint64_t RootCluster = 0;

    // Reserved
    uint16_t Reserved3 = 0;
    uint32_t Reserved4 = 0;

    // End
    char Resv[455] = {"I can't belive you put in the effort to read this!"};
    uint16_t EndSig = 0x0BB0; // :P
} __attribute__((__packed__));

struct ClusterMapEntry{
    // Location
    uint16_t SectorIndex = 0;
    uint64_t StoredCluster = 0;

    // Reserved
    uint16_t Flags = 0;  // () | () | () | () | () | () | () | () | () | () | () | () | () | (FULL) | (RESERVED) | (ALLOCATED)
    uint32_t Reserved1 = 0;
} __attribute__((__packed__));

struct ClusterEntry{
    uint8_t State = 0; // () | () | () | (LFN) | (FILECONTENT) | (DIRECTORY) | (CLUSTERMAP) | (ALLOCATED)

    // Next Location
    uint16_t NextSectorIndex = 0;
    uint64_t NextCluster = 0;

    // Allocate An entire Cluster Easily
    uint16_t AllocationSize = 0;
    uint16_t SectorStart = 0;

    // Reserved
    uint8_t Reserved1 = 0;
} __attribute__((__packed__));

struct DirectoryEntry{
    uint16_t FLAGS = 0; // () | () | () | () | () | () | () | (NoExecute) | (NoREAD) | (NoWRITE) | (EXISTS) | (LINK) | (READONLY) | (DIRECTORY) | (LFN) | (ALLOCATED)
    char FileName[16] = {0}; // Reserved Data (8 Bytes cluster + 2 Sector for LFN)

    // Location of Data
    uint16_t SectorIndex = 0;
    uint64_t StoredCluster = 0;

    uint16_t Year = 0;
    uint32_t Month : 4;
    uint32_t Date : 5;
    uint32_t Hour : 5;
    uint32_t Minute : 6;
    uint32_t Second : 6;
    uint32_t Reserved : 6;

    uint64_t FileSize = 0;
    uint32_t Reserved1 = 0;
} __attribute__((__packed__));