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

    for(int i = 0; i < 32; i++){
        if(ClusterMapEntryList[i].SectorStart == SectorIndex){
            return ClusterMapEntryList[i];
        }
    }

    // Find cluster entry
    return {}; // Error

    // TODO: MultiSector ClusterMaps
}

struct SectorListing{
    uint64_t Sector = 0;
    uint16_t Count = 0;
};

inline std::vector<SectorListing> ParseClusterMapIntoSectors(MasterRecord& FileMaster, FILE* FD, uint64_t StartCluster, uint16_t StartSectorIndex){
    if(StartCluster == 0){
        return {}; // For new allocations
    }

    auto StartClusterEntry = FindClusterEntry(FileMaster, FD, StartCluster, StartSectorIndex);

    if(StartClusterEntry.AllocationSize == 0){
        return {};
    }

    std::vector<SectorListing> ReturnVec = {};
    
    // No need to search for other clusters
    ReturnVec.push_back({StartCluster*FileMaster.ClusterSize + StartSectorIndex, StartClusterEntry.AllocationSize});

    while(StartClusterEntry.NextCluster){
        auto Backup = StartClusterEntry;
        StartClusterEntry = FindClusterEntry(FileMaster, FD, StartClusterEntry.NextCluster, StartClusterEntry.NextSectorIndex);

        if(StartClusterEntry.AllocationSize == 0){
            return {};
        }

        // No need to search for other clusters
        ReturnVec.push_back({Backup.NextCluster*FileMaster.ClusterSize + Backup.NextSectorIndex, StartClusterEntry.AllocationSize});
    }

    return ReturnVec;
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
int AllocateSize(MasterRecord& FileMaster, FILE* FD, uint64_t StartCluster, uint16_t StartSectorIndex, size_t Bytes, size_t PrevSize, uint8_t Flags, ClusterEntry* NewAllocationReturnPoint = nullptr){
    auto CurrentDisk = ParseClusterMapIntoSectors(FileMaster, FD, StartCluster, StartSectorIndex);

    size_t CurrentAllocationSectors = 0;
    for(auto x : CurrentDisk){
        CurrentAllocationSectors+=x.Count;
    }

    bool FreshAllocation = !StartCluster;

    if(!FreshAllocation && Bytes < (512*CurrentAllocationSectors - PrevSize)){
        return 0; // No Need to allocate data
    }

    // Calculate Sectors Needed
    size_t Sectors = 0;
    if(!FreshAllocation){
        Sectors = ceil((Bytes - (512*CurrentAllocationSectors - PrevSize)) / 512.0);
    }
    else{
        Sectors = ceil(Bytes / 512.0);
    }

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
                std::cout << "Multi-sector Cluster Maps Are Unsupported: " << FileMaster.ClusterSize << std::endl;
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
                        Allocations.push_back({(AllocateSize_ClusterMapSearchAllocation[i].StoredCluster * FileMaster.ClusterSize + AllocateSize_ClusterMapSearchAllocation[i].SectorIndex), (uint8_t)j, CurrentSelectedCluster, (uint16_t)j, (uint16_t)fmin(AllocateSize_ClusterSearchEntryList[j].AllocationSize, (uint16_t)RemainingData)});
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

            Allocations.push_back({(AllocateSize_ClusterMapSearchAllocation[i].StoredCluster * FileMaster.ClusterSize + AllocateSize_ClusterMapSearchAllocation[i].SectorIndex), (uint8_t)index, CurrentSelectedCluster, UsedAllocations, (uint16_t)fmin(Available, (uint16_t)RemainingData)});

            if(Available > RemainingData){
                RemainingData = 0;
                Available = Available - RemainingData;
                FoundASuitableRegion = true;
                break;
            }
            else{
                Available = 0;
                RemainingData -= Available;

                // First locate where the cluster map is kept
                auto ClusterMapEnt = FindClusterMapEntry(FileMaster, FD, AllocateSize_ClusterMapSearchAllocation[i].StoredCluster);

                ClusterMapEnt.Flags |= 0b100;

                // Load the list
                fseek(FD, ((FileMaster.ClusterMapOffset + (AllocateSize_ClusterMapSearchAllocation[i].StoredCluster / 32)) * 512) + ((AllocateSize_ClusterMapSearchAllocation[i].StoredCluster % FileMaster.ClusterSize) * sizeof(ClusterMapEnt)), SEEK_SET);
                fwrite((void*)(&ClusterMapEnt), sizeof(ClusterMapEnt), 1, FD);

                if(((AllocateSize_ClusterMapSearchAllocation[i].StoredCluster % FileMaster.ClusterSize) * sizeof(ClusterMapEnt)) > 32){
                    std::cout << "MultiSector ClusterEntries are a STUB" << std::endl;
                    return -1;
                }

                // Marked as full
            }
        }

        CurrentPos+=512 * (!FoundASuitableRegion); // Next Sector
        if((CurrentPos / 512) < (FileMaster.ClusterMapSize/32)*512 + 512){
            break; // End of cluster map (Meaning No More Room LEft)
        }
    }

    if (!FoundASuitableRegion){
        std::cout << "Failed to find suitable region" << std::endl;
        return -1;
    }

    if(!FreshAllocation){
        // Find last cluster entry
        auto FoundClusterEntry = FindClusterEntry(FileMaster, FD, StartCluster, StartSectorIndex);
        uint64_t ClusterAssociated = StartCluster;
        while(FoundClusterEntry.NextCluster){
            ClusterAssociated = FoundClusterEntry.NextCluster;
            FoundClusterEntry = FindClusterEntry(FileMaster, FD, FoundClusterEntry.NextCluster, FoundClusterEntry.NextSectorIndex);
        }

        for(auto x : Allocations){
            // Update old cluster entry
            FoundClusterEntry.NextCluster = x.Cluster;
            FoundClusterEntry.NextSectorIndex = x.Sector;

            { // Update cluster entry with new next sectors
                // First locate where the cluster map is kept
                ClusterEntry ClusterEntriesForSector[32] = {};
                auto ClusterMapEnt = FindClusterMapEntry(FileMaster, FD, ClusterAssociated);

                if((ClusterMapEnt.Flags & 0b1) == 0){
                    return -1;
                }

                // In theory a cluster map should be 1 sector, for Lower ClusterSizes that is (<=32)
                fseek(FD, ((ClusterMapEnt.StoredCluster * FileMaster.ClusterSize + ClusterMapEnt.SectorIndex) * 512), SEEK_SET);
                fread((void*)(ClusterEntriesForSector), 512, 1, FD);
                fseek(FD, ((ClusterMapEnt.StoredCluster * FileMaster.ClusterSize + ClusterMapEnt.SectorIndex) * 512), SEEK_SET);

                for(int i = 0; i < 32; i++){
                    if(ClusterEntriesForSector[i].SectorStart == FoundClusterEntry.SectorStart){
                        // Found the entry
                        ClusterEntriesForSector[i] = FoundClusterEntry;
                        break;
                    }
                }

                fwrite((void*)(ClusterEntriesForSector), 512, 1, FD);
            }

            // Now add new sectorMapEntry Allocation
            ClusterEntry NewEntry = {(uint8_t)(Flags | 0b1), 0, 0, x.Count, (uint16_t)x.Sector, 0};
            {
                // First locate where the cluster map is kept
                ClusterEntry ClusterEntriesForSector[32] = {};
                auto ClusterMapEnt = FindClusterMapEntry(FileMaster, FD, x.Cluster);

                if((ClusterMapEnt.Flags & 0b1) == 0){
                    return -1;
                }

                // In theory a cluster map should be 1 sector, for Lower ClusterSizes that is (<=32)
                fseek(FD, ((ClusterMapEnt.StoredCluster * FileMaster.ClusterSize + ClusterMapEnt.SectorIndex) * 512), SEEK_SET);
                fread((void*)(ClusterEntriesForSector), 512, 1, FD);
                fseek(FD, ((ClusterMapEnt.StoredCluster * FileMaster.ClusterSize + ClusterMapEnt.SectorIndex) * 512), SEEK_SET);

                ClusterEntriesForSector[x.EntryNumber] = NewEntry;

                fwrite((void*)(ClusterEntriesForSector), 512, 1, FD);
            }

            // Move to new entry for adding next one
            FoundClusterEntry = NewEntry;
            ClusterAssociated = x.Cluster;
        }
    }
    else{
        // Find last cluster entry
        ClusterEntry FoundClusterEntry = {};
        uint64_t ClusterAssociated = 0;

        for(auto x : Allocations){
            if(FoundClusterEntry.AllocationSize){
                // Update old cluster entry
                FoundClusterEntry.NextCluster = x.Cluster;
                FoundClusterEntry.NextSectorIndex = x.Sector;

                { // Update cluster entry with new next sectors
                    // First locate where the cluster map is kept
                    ClusterEntry ClusterEntriesForSector[32] = {};
                    auto ClusterMapEnt = FindClusterMapEntry(FileMaster, FD, ClusterAssociated);

                    if((ClusterMapEnt.Flags & 0b1) == 0){
                        return -1;
                    }

                    // In theory a cluster map should be 1 sector, for Lower ClusterSizes that is (<=32)
                    fseek(FD, ((ClusterMapEnt.StoredCluster * FileMaster.ClusterSize + ClusterMapEnt.SectorIndex) * 512), SEEK_SET);
                    fread((void*)(ClusterEntriesForSector), 512, 1, FD);
                    fseek(FD, ((ClusterMapEnt.StoredCluster * FileMaster.ClusterSize + ClusterMapEnt.SectorIndex) * 512), SEEK_SET);

                    for(int i = 0; i < 32; i++){
                        if(ClusterEntriesForSector[i].SectorStart == FoundClusterEntry.SectorStart){
                            // Found the entry
                            ClusterEntriesForSector[i] = FoundClusterEntry;
                            break;
                        }
                    }

                    fwrite((void*)(ClusterEntriesForSector), 512, 1, FD);
                }
            }

            // Now add new sectorMapEntry Allocation
            ClusterEntry NewEntry = {(uint8_t)(Flags | 0b1), 0, 0, x.Count, (uint16_t)x.Sector, 0};
            {
                // First locate where the cluster map is kept
                ClusterEntry ClusterEntriesForSector[32] = {};
                auto ClusterMapEnt = FindClusterMapEntry(FileMaster, FD, x.Cluster);

                if((ClusterMapEnt.Flags & 0b1) == 0){
                    return -1;
                }

                // In theory a cluster map should be 1 sector, for Lower ClusterSizes that is (<=32)
                fseek(FD, ((ClusterMapEnt.StoredCluster * FileMaster.ClusterSize + ClusterMapEnt.SectorIndex) * 512), SEEK_SET);
                fread((void*)(ClusterEntriesForSector), 512, 1, FD);
                fseek(FD, ((ClusterMapEnt.StoredCluster * FileMaster.ClusterSize + ClusterMapEnt.SectorIndex) * 512), SEEK_SET);

                ClusterEntriesForSector[x.EntryNumber] = NewEntry;

                fwrite((void*)(ClusterEntriesForSector), 512, 1, FD);
                
                if(NewAllocationReturnPoint->AllocationSize == 0){
                    NewAllocationReturnPoint[0] = {0, x.EntryNumber, x.Cluster, x.Count, 0, 0};
                }
            }

            // Move to new entry for adding next one
            FoundClusterEntry = NewEntry;
            ClusterAssociated = x.Cluster;
        }
    }


    return 0;
}