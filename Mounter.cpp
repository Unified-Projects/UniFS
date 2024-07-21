#define FUSE_USE_VERSION 51

#include <fuse_lowlevel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <pthread.h>
#include <sys/time.h>

#include <string>
#include <iostream>
#include <vector>

#include "FilesystemStuff/Structure.h"
#include "FilesystemStuff/Helpers.h"

FILE* Disk;
long fileSize = 0;
char* FilePath = nullptr;
MasterRecord FileMaster = {};

struct inoMapEntry{
	uint64_t Ino = 0;
	uint64_t ParentIno = 0;
	DirectoryEntry File = {};
	uint64_t SectorOfSettings = 0;
	uint64_t EntryNumber = 0;
};

std::vector<inoMapEntry> InoMap = {};

uint64_t NextAllocatableIno = 2;

bool InitIno = false;
inoMapEntry RDE = {};
inoMapEntry& GetIno(uint64_t Ino){
	if(Ino == 1) {
		if(!InitIno){
			InitIno = true;
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

			RDE = {1, 1, {0b100101, {0}, FileMaster.RootSectorIndex, FileMaster.RootCluster, year, month, day, hour, minute, second, 0, 0, 0}, 0, 0};
		}
		return RDE;
	}

	for (auto& x : InoMap){
		if (x.Ino == Ino){
			return x;
		}
	}

	// Try locate from disk
	// TODO

	static inoMapEntry StaticEnt = {};

	return StaticEnt;
}

// Function to count the number of '/' in a string
size_t countSlashes(const std::string& str) {
    size_t count = 0;
    for (char ch : str) {
        if (ch == '/') {
            ++count;
        }
    }
    return count;
}

// Function to check if 'str' begins with 'prefix' and contains the same number of '/'
bool beginsWithAndEqualSlashes(const std::string& str, const std::string& prefix) {
    // Check if 'str' begins with 'prefix'
    if (str.find(prefix) != 0) {
        return false;
    }
    
    // Check if the number of '/' in both strings is equal
    return (countSlashes(str) == (countSlashes(prefix) + ((prefix.size() > 1) ? 1 : 0)));
}

std::string getParentDirectory(const std::string& filePath) {
    // Find the last occurrence of '/'
    size_t found = filePath.find_last_of('/');
    
    // If '/' is found, extract substring from start to the position before it
    if (found != std::string::npos) {
        return filePath.substr(0, found - ((found > 1) ? 1 : 0));
    } else {
        // If '/' is not found, return an empty string or handle as per requirement
        return "";
    }
}

static int UniFS_stat(fuse_ino_t ino, struct stat *stbuf)
{
	stbuf->st_ino = ino;

	// Get Entry
	auto ref = GetIno(ino).File;

	if((ref.FLAGS & 0b100000) == 0){
		std::cout << "Err" << std::endl;
		return -1;
	}

	if((ref.FLAGS & 0b100) == 0){
		stbuf->st_mode = S_IFREG | 0777; // 777 Complete access
		stbuf->st_nlink = 1;
		stbuf->st_size = ref.FileSize;
	}
	else{
		stbuf->st_mode = S_IFDIR | 0777;
		stbuf->st_nlink = 2;
	}

	// Fill the tm structure
    struct tm timeinfo = {0};
    timeinfo.tm_mday = ref.Date;
    timeinfo.tm_mon = ref.Month - 1; // tm_mon is 0-based (0 = January, 11 = December)
    timeinfo.tm_year = ref.Year - 1900; // tm_year is the number of years since 1900
    timeinfo.tm_hour = ref.Hour;
    timeinfo.tm_min = ref.Minute;
    timeinfo.tm_sec = ref.Second;

    // Convert tm to time_t
    time_t timeInSeconds = mktime(&timeinfo);

	stbuf->st_atimespec = {timeInSeconds, 0};
	stbuf->st_mtimespec = {timeInSeconds, 0};
	stbuf->st_ctimespec = {timeInSeconds, 0};

	stbuf->st_blocks = 0; // TODO

	return 0;
}

static void UniFS_getattr(fuse_req_t req, fuse_ino_t ino,
			     struct fuse_file_info *fi)
{
	struct stat stbuf;

	(void) fi;

	memset(&stbuf, 0, sizeof(stbuf));
	if (UniFS_stat(ino, &stbuf) == -1)
		fuse_reply_err(req, ENOENT);
	else
		fuse_reply_attr(req, &stbuf, 1.0);
}

static void UniFS_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
	struct fuse_entry_param e;

	for(auto x : InoMap){
		if(x.ParentIno == parent){
			if(x.File.FLAGS & 0b10){
				std::cout << "LFN are a STUB" << std::endl;
				continue;
			}

			// Check name
			if(strlen(name) == strlen(x.File.FileName)){
				if(!memcmp(name, x.File.FileName, strlen(name))){
					// Found it
					memset(&e, 0, sizeof(e));
					e.ino = x.Ino;
					e.attr_timeout = 1.0;
					e.entry_timeout = 1.0;
					UniFS_stat(e.ino, &e.attr);

					fuse_reply_entry(req, &e);
					return;
				}
			}

			// Could beign with "._"
			if(strlen(name + 2) == strlen(x.File.FileName)){
				if(!memcmp(name + 2, x.File.FileName, strlen(name + 2))){
					// Found it
					memset(&e, 0, sizeof(e));
					e.ino = x.Ino;
					e.attr_timeout = 1.0;
					e.entry_timeout = 1.0;
					UniFS_stat(e.ino, &e.attr);

					fuse_reply_entry(req, &e);
					return;
				}
			}
		}
	}

	fuse_reply_err(req, ENOENT);
}

struct dirbuf {
	char *p;
	size_t size;
};

static void dirbuf_add(fuse_req_t req, struct dirbuf *b, const char *name,
		       fuse_ino_t ino, bool dir, size_t FileSize)
{
	struct stat stbuf;
	memset(&stbuf, 0, sizeof(stbuf));
	UniFS_stat(ino, &stbuf);
	size_t oldsize = b->size;
	b->size += fuse_add_direntry(req, NULL, 0, name, NULL, 0);
        char *newp = (char*)realloc(b->p, b->size);
        if (!newp) {
            fprintf(stderr, "*** fatal error: cannot allocate memory\n");
            abort();
        }
	b->p = newp;
	
	fuse_add_direntry(req, b->p + oldsize, b->size - oldsize, name, &stbuf,
			  b->size);
}

#define min(x, y) ((x) < (y) ? (x) : (y))

static int reply_buf_limited(fuse_req_t req, const char *buf, size_t bufsize,
			     off_t off, size_t maxsize)
{
	if (off < bufsize)
		return fuse_reply_buf(req, buf + off,
				      min(bufsize - off, maxsize));
	else
		return fuse_reply_buf(req, NULL, 0);

	fuse_reply_err(req, ENOTSUP);
}


uint8_t SectorStore[512];
static void UniFS_readdir(fuse_req_t req, fuse_ino_t ino, size_t size,
			     off_t off, struct fuse_file_info *fi)
{
	(void) fi;
	struct dirbuf b;

	auto Dir = GetIno(ino);
	if(Dir.File.FLAGS == 0){
		// fuse_reply_err(req, EINVAL);
		std::cout << "ReadDIr ERR" << std::endl;
	}

	memset(&b, 0, sizeof(b));
	dirbuf_add(req, &b, ".", ino, true, 0);
	dirbuf_add(req, &b, "..", Dir.ParentIno, true, 0);

	// Load Directory Listing
	auto DirSectors = ParseClusterMapIntoSectors(FileMaster, Disk, Dir.File.StoredCluster, Dir.File.SectorIndex);

	for(auto x : DirSectors){
		for(int i = 0; i < x.Count; i++){
			// Read sector off disk
			fseek(Disk, (x.Sector + i) * 512, SEEK_SET);
			fread((void*)SectorStore, 512, 1, Disk);

			// Parse into directory entries (11 per sector)
			DirectoryEntry* Dirs = (DirectoryEntry*)(SectorStore);

			for(int j = 0; j < 11; j++){
				if(!(Dirs[j].FLAGS & 0b100000)){ // Is it Exists
					continue;
				}

				// Found a file entry in the folder
				if(!(Dirs[j].FLAGS & 0b10)){ // Contained FileName
					// Do we have it already...
					bool Done = false;
					for (auto x : InoMap){
						if (x.ParentIno == Dir.Ino && !memcmp((void*)(&x.File), (void*)(&Dirs[j]), sizeof(DirectoryEntry))){
							dirbuf_add(req, &b, strdup(Dirs[j].FileName), x.Ino, ((x.File.FLAGS & 0b100) > 0), x.File.FileSize);
							Done=true;
							break;
						}
					}
					
					if(!Done){
						// No Then create it
						InoMap.push_back({NextAllocatableIno++, Dir.Ino, Dirs[j], (x.Sector + i), (uint64_t)j});
						dirbuf_add(req, &b, strdup(Dirs[j].FileName), NextAllocatableIno-1, ((Dirs[j].FLAGS & 0b100) > 0), Dirs[j].FileSize);
					}
				}

				// LFN TODO
			}
		}
	}

	reply_buf_limited(req, b.p, b.size, off, size);
	free(b.p);
}

static void UniFS_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi) {
    auto Ent = GetIno(ino);

    if (!Ent.File.FLAGS) {
        fuse_reply_err(req, ENOENT);
        return;
    }

    if (Ent.File.FLAGS & 0b100) {
        fuse_reply_err(req, EISDIR);
        return;
    }

    fuse_reply_open(req, fi);
}

static void UniFS_read(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi) {
    auto Ent = GetIno(ino);

    if (!Ent.File.FLAGS) {
        fuse_reply_err(req, ENOENT);
        return;
    }

    if (Ent.File.FLAGS & 0b100) {
        fuse_reply_err(req, EISDIR);
        return;
    }

	// Get File Sectors
	auto DirSectors = ParseClusterMapIntoSectors(FileMaster, Disk, Ent.File.StoredCluster, Ent.File.SectorIndex);

    char *buf = new char[size];
    memset(buf, 0, size);

	uint64_t BytesRead = 0;

	// Read on sectors
	for(auto s : DirSectors){
		fseek(Disk, s.Sector * 512, SEEK_SET);
		fread(buf + BytesRead, min(512*s.Count, size-BytesRead), 1, Disk);
		BytesRead += min(512*s.Count, size-BytesRead);

		if(BytesRead == 0){
			break; // Just incase its been overallocated
		}
	}

    reply_buf_limited(req, buf, size, off, size);

    delete[] buf;
}

static void UniFS_write(fuse_req_t req, fuse_ino_t ino, const char *buf,
		       size_t size, off_t off, struct fuse_file_info *fi) {
    auto& Ent = GetIno(ino);

	if (ceil(Ent.File.FileSize/512.0) < ceil((off + size) / 512.0)){
		// Need to allocate sectors.
		std::cout << "STUB: CANNOT ALLOCATE SECTORS" << std::endl;
	}

	// Get File Sectors
	auto DirSectors = ParseClusterMapIntoSectors(FileMaster, Disk, Ent.File.StoredCluster, Ent.File.SectorIndex);

	uint64_t BytesWritten = 0;
	uint64_t PosInFile = 0;
	uint16_t SectorStartOffset = off%512;

	// Read on sectors
	for(auto s : DirSectors){
		uint16_t SectorsIn = 0;
		if (PosInFile <= off){
			if(PosInFile + s.Count * 512 > off){
				while(PosInFile + 512 < off){
					SectorsIn++;
				}
			}else{
				continue; // Not in this block
			}
		}

		fseek(Disk, (s.Sector + SectorsIn) * 512 + SectorStartOffset, SEEK_SET);
		SectorStartOffset = 0; // Zero Any Set Offset

		fwrite(buf + BytesWritten, min(512*(s.Count - SectorsIn), size-BytesWritten), 1, Disk);
		BytesWritten += min(512*(s.Count - SectorsIn), size-BytesWritten);
		fflush(Disk);

		if(BytesWritten == 0){
			break; // Just incase its been overallocated
		}
	}

	// New FileSize
	uint64_t NewFileSize = fmax(size + off, Ent.File.FileSize);

	Ent.File.FileSize = NewFileSize;

	// Get current time
	std::time_t now = std::time(nullptr);
	std::tm* localTime = std::localtime(&now);

	// Extract date and time components
	Ent.File.Year = 1900 + localTime->tm_year;         // tm_year is years since 1900
	Ent.File.Month = 1 + localTime->tm_mon;            // tm_mon is 0-based (0 = January)
	Ent.File.Date = localTime->tm_mday;
	Ent.File.Hour = localTime->tm_hour;
	Ent.File.Minute = localTime->tm_min;
	Ent.File.Second = localTime->tm_sec;

	std::cout << "HMM " << (Ent.SectorOfSettings * 512) + (Ent.EntryNumber * sizeof(DirectoryEntry)) << std::endl;

	fseek(Disk, (Ent.SectorOfSettings * 512) + (Ent.EntryNumber * sizeof(DirectoryEntry)), SEEK_SET);
	fwrite((void*)(&Ent.File), sizeof(DirectoryEntry), 1, Disk);
	fflush(Disk);

	fuse_reply_write(req, (size_t) size);
}

static void UniFS_create(fuse_req_t req, fuse_ino_t parent, const char *name, mode_t mode, struct fuse_file_info *fi) {
	std::cout << "Create: " << name << std::endl;

	for(auto x : InoMap){
		if (x.ParentIno == parent){
			// TODO LFN

			if(strlen(name) == strlen(x.File.FileName)){
				if(!memcmp(name, x.File.FileName, strlen(name))){
					// Already present
					fuse_reply_err(req, EALREADY);
					return;
				}
			}
		}
	}

	// Is a new file
	// Parent should be a directory
	// TODO CONFIRM
	// Get File Sectors
	auto DirSectors = ParseClusterMapIntoSectors(FileMaster, Disk, GetIno(parent).File.StoredCluster, GetIno(parent).File.SectorIndex);

	for(auto x : DirSectors){
		for(int i = 0; i < x.Count; i++){
			// Read sector off disk
			fseek(Disk, (x.Sector + i) * 512, SEEK_SET);
			fread((void*)SectorStore, 512, 1, Disk);

			// Parse into directory entries (11 per sector)
			DirectoryEntry* Dirs = (DirectoryEntry*)(SectorStore);

			for(int j = 0; j < 11; j++){
				if((Dirs[j].FLAGS & 0b100000)){ // If it Exists, Skip
					continue;
				}

				// Found a empty entry
				Dirs[j] = {
					(uint16_t)(0b100000 |(((mode & S_IFDIR) > 0) * 0b100) | (((mode & S_IFLNK) > 0) * 0b1000)), // FLAGS FOR TYPES
					{0},
					0, 0, // No Data
					0, 0, 0, 0, 0, 0, 0, // Date and Time (TBC)
					0, // File Size
					0 // Reserved
				};

				// Get current time
				std::time_t now = std::time(nullptr);
				std::tm* localTime = std::localtime(&now);

				// Extract date and time components
				Dirs[j].Year = 1900 + localTime->tm_year;         // tm_year is years since 1900
				Dirs[j].Month = 1 + localTime->tm_mon;            // tm_mon is 0-based (0 = January)
				Dirs[j].Date = localTime->tm_mday;
				Dirs[j].Hour = localTime->tm_hour;
				Dirs[j].Minute = localTime->tm_min;
				Dirs[j].Second = localTime->tm_sec;

				if(strlen(name) > 16){
					std::cout << "Cannot Use LFN on new file!" << std::endl;
				}
				
				strncpy(Dirs[j].FileName, name, min(16, strlen(name)));

				InoMap.push_back({NextAllocatableIno++, parent, Dirs[j], x.Sector + i, (uint64_t)j});

				fseek(Disk, (x.Sector + i) * 512, SEEK_SET);
				fwrite((void*)(SectorStore), 512, 1, Disk);
				fflush(Disk);

				struct fuse_entry_param e;
				memset(&e, 0, sizeof(e));
				e.ino = NextAllocatableIno-1;
				e.attr_timeout = 1.0;
				e.entry_timeout = 1.0;
				UniFS_stat(e.ino, &e.attr);

				fuse_reply_create(req, &e, fi);
				return;

				// TODO LFN
			}
		}
	}

	// If we made it here we need to allocate a new sector!
	fuse_reply_err(req, ENOSPC);
}

static void UniFS_access(fuse_req_t req, fuse_ino_t ino, int mask) {
    auto Ent = GetIno(ino);

    if (!Ent.File.FLAGS) {
        fuse_reply_err(req, ENOENT);
        return;
    }

    // Check if the file or directory can be accessed with the given mask
    // if ((mask & R_OK) && !(Ent.File.FLAGS & 0b10000000)) { // Read permission
    //     fuse_reply_err(req, EACCES);
    //     return;
    // }
    // if ((mask & W_OK) && !(Ent.File.FLAGS & 0b1001000)) { // Write permission
    //     fuse_reply_err(req, EACCES);
    //     return;
    // }
    // if ((mask & X_OK) && !(Ent.File.FLAGS & 0b100000000)) { // Execute permission
    //     fuse_reply_err(req, EACCES);
    //     return;
    // }

	// TODO

    fuse_reply_err(req, 0);
}

static void UniFS_setxattr(fuse_req_t req, fuse_ino_t ino, const char *name, const char *value, size_t size, int flags, uint32_t) {
    auto Ent = GetIno(ino);

    if (!Ent.File.FLAGS) {
        fuse_reply_err(req, ENOENT);
        return;
    }

    // Implement logic to set the extended attribute
    // For now, just printing out the attributes as a placeholder
    std::cout << "Setxattr: Inode " << ino << ", Name: " << name << ", Value: " << value << ", Size: " << size << std::endl;

	// TODO
    fuse_reply_err(req, 0);
}

static void UniFS_release(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi) {
    (void) fi; // Unused parameter

    std::cout << "Release request received for inode: " << ino << std::endl;
    
    fuse_reply_err(req, 0); // Indicate successful release
}

static void UniFS_releaseDir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi) {
    (void) fi; // Unused parameter

    std::cout << "Release request received for inode: " << ino << std::endl;
    
    fuse_reply_err(req, 0); // Indicate successful release
}

static void UniFS_fsync(fuse_req_t req, fuse_ino_t ino, int datasync, struct fuse_file_info *fi) {
    (void) fi; // Unused parameter

    std::cout << "fsync request received for inode: " << ino << std::endl;
    
    if (datasync) {
        std::cout << "Data sync request, only data will be synchronized" << std::endl;
        // Perform data synchronization if required
    } else {
        std::cout << "Full sync request, metadata and data will be synchronized" << std::endl;
        // Perform full synchronization
    }

	// TODO

    fuse_reply_err(req, 0); // Indicate successful synchronization
}

static void UniFS_flush(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi) {
    fuse_reply_err(req, 0);
}

static struct fuse_lowlevel_ops UniFS_oper = {
	.readdir	= UniFS_readdir,
	.create		= UniFS_create,
	.flush		= UniFS_flush,
	.fsync		= UniFS_fsync,

	// Attributes
	.access		= UniFS_access,
	.lookup		= UniFS_lookup,
	.setxattr	= UniFS_setxattr,
	.getattr	= UniFS_getattr,

	// Read Writes
	.read		= UniFS_read,
	.write		= UniFS_write,

	// Access Controlls
	.open		= UniFS_open,
	.release	= UniFS_release,
	.releasedir	= UniFS_releaseDir,

	// TODOS
};

int main(int argc, char *argv[])
{
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	struct fuse_chan *ch;
	char *mountpoint;
	int err = 0;

    // fuse_parse_cmdline(&args, &mountpoint, NULL, NULL);
	if (fuse_parse_cmdline(&args, &FilePath, NULL, NULL) != -1) {
		// Adapt Mountpoint
		mountpoint = strdup(FilePath);
		mountpoint[strlen(mountpoint) - 4] = 0;

		// Load ramdisk
		Disk = fopen(FilePath, "r+");
		fileSize = 0;
		fseek(Disk, 0, SEEK_END);
		fileSize = ftell(Disk);
		rewind(Disk);

		fread((void*)(&FileMaster), sizeof(MasterRecord), 1, Disk);
		
		// Validate
		if(memcmp(FileMaster.sig, "UNIFIED", 7)){
			fclose(Disk);
			std::cout << "Failed to Validate Ramdisk" << std::endl;
			return 1;
		}

		if((ch = fuse_mount(mountpoint, &args)) == NULL){
			return 1;
		}

		struct fuse_session *se;

		se = fuse_lowlevel_new(&args, &UniFS_oper,
				       sizeof(UniFS_oper), NULL);
		if (se != NULL) {
			if (fuse_set_signal_handlers(se) != -1) {
				fuse_session_add_chan(se, ch);

				if (!err) {
					err = fuse_session_loop(se);
				}

				fuse_remove_signal_handlers(se);
				fuse_session_remove_chan(ch);
			}
			fuse_session_destroy(se);
		}
		fuse_unmount(mountpoint, ch);
	}
	fuse_opt_free_args(&args);

	fclose(Disk);

	return err ? 1 : 0;
}