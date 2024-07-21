#pragma once

#include "Structure.h"

#include <stdio.h>
#include <vector>

// TODO, Most code assumes zeroed disk, make sure that if it is allocated it is zeroed.

ClusterMapEntry ClusterEntryList[32] = {};
inline ClusterMapEntry FindClusterMapEntry(MasterRecord& FileMaster, FILE* FD, uint64_t Cluster){
    // Calculate Expected Sector
    uint64_t ExpectedSector = FileMaster.ClusterMapOffset + (Cluster / 32);

    if(Cluster > FileMaster.ClusterMapSize){
        return {};
    }

    // Load the list
    fseek(FD, ExpectedSector * 512, SEEK_SET);
    fread((void*)(ClusterEntryList), 512, 1, FD);

    // Find the entry
    uint8_t Index = Cluster % 32;

    return ClusterEntryList[Index];
}

ClusterEntry ClusterMapEntryList[32] = {};
inline ClusterEntry FindClusterEntry(MasterRecord& FileMaster, FILE* FD, uint64_t Cluster, uint64_t SectorIndex) {
    // First locate where the cluster map is kept
    auto ClusterMapEnt = FindClusterMapEntry(FileMaster, FD, Cluster);

    if((ClusterMapEnt.Flags & 0b1) == 0){
        return {};
    }

    // In theory a cluster map should be 1 sector, for Lower ClusterSizes that is (<=32)
    fseek(FD, (ClusterMapEnt.StoredCluster * FileMaster.ClusterSize + ClusterMapEnt.SectorIndex) * 512, SEEK_SET);
    fread((void*)(ClusterMapEntryList), 512, 1, FD);

    if(SectorIndex > 32){
        std::cout << "MultiSectore ClusterEntries are a STUB" << std::endl;
        return {};
    }

    // Find cluster entry
    return ClusterMapEntryList[SectorIndex];

    // TODO: MultiSector ClusterMaps
}

void AllocateClusterMap(MasterRecord& FileMaster, FILE* FD, uint64_t StartCluster, uint16_t StartSectorIndex){

}

ClusterMapEntry AllocateSize_ClusterMapSearchAllocation[32] = {};
ClusterEntry AllocateSize_ClusterSearchEntryList[32] = {};
ClusterEntry AllocateSize_ClusterUpdateEntry = {};
struct AllocatedRegion{
    // Map Entry Byte Location
    uint64_t LocationOfSector = 0;
    uint8_t EntryNumber = 0;

    uint64_t Cluster = 0;
    uint16_t Sector = 0;
    uint16_t Count = 0;
};
int AllocateSize(MasterRecord& FileMaster, FILE* FD, uint64_t StartCluster, uint16_t StartSectorIndex, size_t Bytes, uint8_t Flags){
    // Calculate Sectors Needed
    size_t Sectors = floor(Bytes / 512.0); // Floored as if not a multiple of 512 there is still data available to use in sector
    size_t RemainingData = Sectors;

    if(RemainingData == 0){
        return 0; // Success
    }

    std::vector<AllocatedRegion> Allocations;

    bool FoundASuitableRegion = false;
    uint64_t CurrentPos = FileMaster.ClusterMapOffset * 512;
    // Look for any allocated ClusterMaps
    while(!FoundASuitableRegion){
        // Read data
        fseek(FD, CurrentPos, SEEK_SET);
        fread((void*)(AllocateSize_ClusterMapSearchAllocation), 512, 1, FD);

        // Look over clusters
        for(int i = 0; i < 32; i++){
            if((AllocateSize_ClusterMapSearchAllocation[i].Flags & 0b111) != 0b001){
                // Not usable
                continue;
            }

            uint64_t CurrentSelectedCluster = ((CurrentPos - 512) / 32) + i;

            // See how much space is left in its cluster
            if(FileMaster.ClusterSize > 32){
                std::cout << "Multi-sector Cluster Maps Are Unsupported" << std::endl;
                continue;
            }
            uint16_t UsedAllocations = 0;
            fseek(FD, (AllocateSize_ClusterMapSearchAllocation[i].StoredCluster * FileMaster.ClusterSize + AllocateSize_ClusterMapSearchAllocation[i].SectorIndex) * 512, SEEK_SET);
            fread((void*)AllocateSize_ClusterSearchEntryList, 512, 1, FD);
            short index = -1;

            for(int j = 0; j < 32; j++){
                if(AllocateSize_ClusterSearchEntryList[j].State & 0b1)
                    UsedAllocations += AllocateSize_ClusterSearchEntryList[j].AllocationSize;
                else if(index < 0){
                    if(AllocateSize_ClusterSearchEntryList[j].AllocationSize){ // Was Allocated But Not Anymore
                        Allocations.push_back({(AllocateSize_ClusterMapSearchAllocation[i].StoredCluster * FileMaster.ClusterSize + AllocateSize_ClusterMapSearchAllocation[i].SectorIndex), (uint8_t)j, CurrentSelectedCluster, (uint16_t)j, AllocateSize_ClusterSearchEntryList[j].AllocationSize});
                    }
                    else{
                        index = j;
                    }
                }
            }

            if(UsedAllocations == FileMaster.ClusterSize){
                // Is Full
                continue;
            }

            // Allocate All that is needed
            uint16_t Available = FileMaster.ClusterSize - UsedAllocations;

            Allocations.push_back({(AllocateSize_ClusterMapSearchAllocation[i].StoredCluster * FileMaster.ClusterSize + AllocateSize_ClusterMapSearchAllocation[i].SectorIndex), (uint8_t)index, CurrentSelectedCluster, UsedAllocations, Available});

            if(Available > RemainingData){
                RemainingData = 0;
                Available = Available - RemainingData;
                FoundASuitableRegion = true;
                break;
            }
            else{
                Available = 0;
                RemainingData -= Available;
            }
        }

        CurrentPos+=512 * (!FoundASuitableRegion); // Next Sector
        if((CurrentPos / 512) < (FileMaster.ClusterMapSize/32)*512 + 512){
            break; // End of cluster map (Meaning No More Room LEft)
        }
    }

    if (FoundASuitableRegion){
        for(auto x : Allocations){
            std::cout << "Would have allocated: " << x.Cluster << " " << x.Sector << " " << x.Count << std::endl;
        }
    }

    std::cout << "Allocating New Clusters Is Unsupported Right Now" << std::endl;

    return -1;
}

struct SectorListing{
    uint64_t Sector = 0;
    uint16_t Count = 0;
};

inline std::vector<SectorListing> ParseClusterMapIntoSectors(MasterRecord& FileMaster, FILE* FD, uint64_t StartCluster, uint16_t StartSectorIndex){
    auto StartClusterEntry = FindClusterEntry(FileMaster, FD, StartCluster, StartSectorIndex);

    std::vector<SectorListing> ReturnVec = {};

    if(StartClusterEntry.NextCluster == 0 && StartClusterEntry.AllocationSize > 0){
        // No need to search for other clusters
        ReturnVec.push_back({StartCluster*FileMaster.ClusterSize + StartSectorIndex, StartClusterEntry.AllocationSize});

        return ReturnVec;
    }

    std::cout << "STUB HIT: " << __FILE__ << "::" << __LINE__ << std::endl;

    return {};
}