#include <iostream>
#include <fstream>

#include "FilesystemStuff/Structure.h"

int main() {
    const std::string filename = "UniFS.img";
    const std::streamsize size = 1 * 1024 * 1024; // 512 MB

    // Create and open a binary file for output
    std::ofstream file(filename, std::ios::binary);
    
    // Check if the file was created successfully
    if (!file) {
        std::cerr << "Error: Could not create file " << filename << std::endl;
        return 1;
    }

    // Allocate space by writing zeros
    file.seekp(size - 1);
    file.put('\0');

    // Jump Back to the Begining
    file.seekp(0);

    // Create Master Record For Disk
    MasterRecord FSMaster = {}; // Sector 0

    uint64_t TotalSectorCount = size / 512;
    uint64_t ExpectedClusterCount = (TotalSectorCount / FSMaster.ClusterSize) + ((TotalSectorCount % FSMaster.ClusterSize) > 0);
    uint64_t ExpectedClusterSectors = ((ExpectedClusterCount * sizeof(ClusterEntry)) / 512) + (((ExpectedClusterCount * sizeof(ClusterEntry)) % 512) > 0);
    uint64_t ClusterCountForReservedSpace = (ExpectedClusterSectors + FSMaster.ClusterMapOffset /*MasterRecord*/) / FSMaster.ClusterSize + (((ExpectedClusterSectors + FSMaster.ClusterMapOffset /*MasterRecord*/) % FSMaster.ClusterSize) > 0);

    FSMaster.ClusterMapSize = ExpectedClusterCount;

    FSMaster.RootCluster = ClusterCountForReservedSpace; // First cluster available is a allocation cluster for future cluster maps
    FSMaster.RootSectorIndex = ceil((FSMaster.ClusterSize * sizeof(ClusterEntry)) / 512.f);

    file.write((const char*)(&FSMaster), sizeof(MasterRecord));

    // Write out reserved cluster map
    ClusterMapEntry ReservedEntry = {0, 0, 0b110, 0};
    for(int i = 0; i < ClusterCountForReservedSpace; i++){
        file.write((const char*)(&ReservedEntry), sizeof(ClusterMapEntry));
    }

    // Iniial Cluster
    ClusterMapEntry InitialEntry = {0, ClusterCountForReservedSpace, 0b1, 0};
    file.write((const char*)(&InitialEntry), sizeof(ClusterMapEntry));

    // Write out rest of cluster map
    ClusterMapEntry EmptyEntry = {0, 0, 0, 0};
    for(int i = 0; i < ExpectedClusterCount - ClusterCountForReservedSpace - 1 /*For Initial Cluster*/; i++){
        file.write((const char*)(&EmptyEntry), sizeof(ClusterMapEntry));
    }

    // Seek to Initial Cluster Map Entry
    file.seekp(ClusterCountForReservedSpace * FSMaster.ClusterSize * 512);

    // Create Map Allocation
    ClusterEntry CurrentMapEntry = {0b11, 0, 0, 1, 0, 0};

    // Write 
    file.write((const char*)(&CurrentMapEntry), sizeof(ClusterEntry));

    // Create RDE Map Allocation
    ClusterEntry RootMapEntry = {0b101, 0, 0, 1, 1, 0};

    // Write 
    file.write((const char*)(&RootMapEntry), sizeof(ClusterEntry));

    // Create Hello File Map Allocation
    ClusterEntry HelloEntry = {0b1001, 0, 0, 1, 2, 0};

    // Write 
    file.write((const char*)(&HelloEntry), sizeof(ClusterEntry));

    // Move to rootDirectory
    file.seekp((ClusterCountForReservedSpace * FSMaster.ClusterSize * 512) + 512);

    // Get current time
    std::time_t now = std::time(nullptr);
    std::tm* localTime = std::localtime(&now);

    // Extract date and time components
    uint16_t year = 1900 + localTime->tm_year;         // tm_year is years since 1900
    uint32_t month = 1 + localTime->tm_mon;            // tm_mon is 0-based (0 = January)
    uint32_t dayOfYear = localTime->tm_yday + 1;       // tm_yday is 0-based day of the year
    uint32_t day = localTime->tm_mday;
    uint32_t hour = localTime->tm_hour;
    uint32_t minute = localTime->tm_min;
    uint32_t second = localTime->tm_sec;

    // Create a Hello.txt file
    DirectoryEntry HelloTxt = {0b100000, "Hello.txt", 2, ClusterCountForReservedSpace, year, month, day, hour, minute, second, 0, 24, 0};

    file.write((const char*)(&HelloTxt), sizeof(DirectoryEntry));

    // Move to Hello.txt Contnets
    file.seekp((ClusterCountForReservedSpace * FSMaster.ClusterSize * 512) + 1024);
    file.write("Hello World, From UniFS!", 24);

    // Close the file
    file.close();

    // Confirm the file creation
    if (file) {
        std::cout << "Successfully created " << filename << " with size " << size << " bytes." << std::endl;
    } else {
        std::cerr << "Error: Could not finalize file " << filename << std::endl;
        return 1;
    }

    return 0;
}
