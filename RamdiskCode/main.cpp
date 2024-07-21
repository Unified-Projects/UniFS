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

#include "Packager.h"

FILE* Ramdisk;
char* DATA;
FileInfo Disk = {};
char *FilePath;

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

	DictionaryInfoHeader* Info = nullptr;
	for(auto d : Disk.Dictionary){
		if(ino == d.ino){
			Info = &d;
			break;
		}
	}

	if(Info == nullptr){
		return -1;
	}

	if(Info->type){
		stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_nlink = 1;
		stbuf->st_size = Info->DataSize;
	}
	else{
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	}

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

std::string replaceUnderscoreDotWithSlash(const std::string& input) {
    std::string result = input;
    size_t pos = result.find("._");
    
    while (pos != std::string::npos) {
        result.replace(pos, 2, "/");
        pos = result.find("._", pos + 1);
    }
    
    return result;
}

static void UniFS_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
	struct fuse_entry_param e;

	DictionaryInfoHeader* ParentIno = nullptr;
	for(auto d : Disk.Dictionary){
		if (d.ino == parent){
			ParentIno = &d;
			break;
		}
	}

	if(!ParentIno){
		fuse_reply_err(req, ENOENT);
	}
	else{
		for(auto d : Disk.Dictionary){
			if(d.pathSize <= ParentIno->pathSize || (d.pathSize - ParentIno->pathSize - 1) != strlen(name)){
				continue;
			}

			std::string strName = replaceUnderscoreDotWithSlash(std::string(name));

			if (beginsWithAndEqualSlashes(d.FilePath, ParentIno->FilePath)){
				if(memcmp(d.FilePath.substr(d.FilePath.size() - strName.size()).data(), strName.data(), strName.size())){
					// std::cout << d.FilePath.substr(d.FilePath.size() - strName.size()).data() << " " << strName.data() << std::endl;
					continue;
				}

				memset(&e, 0, sizeof(e));
				e.ino = d.ino;
				e.attr_timeout = 1.0;
				e.entry_timeout = 1.0;
				UniFS_stat(e.ino, &e.attr);
				break;
			}
		}
	}

	fuse_reply_entry(req, &e);
}

struct dirbuf {
	char *p;
	size_t size;
};

static void dirbuf_add(fuse_req_t req, struct dirbuf *b, const char *name,
		       fuse_ino_t ino, bool dir, int file_size)
{
	struct stat stbuf;
	size_t oldsize = b->size;
	b->size += fuse_add_direntry(req, NULL, 0, name, NULL, 0);
        char *newp = (char*)realloc(b->p, b->size);
        if (!newp) {
            fprintf(stderr, "*** fatal error: cannot allocate memory\n");
            abort();
        }
	b->p = newp;
	memset(&stbuf, 0, sizeof(stbuf));
	stbuf.st_ino = ino;
	if (dir) {
        stbuf.st_mode = S_IFDIR | 0755;
        stbuf.st_nlink = 2;
    } else {
		stbuf.st_mode = S_IFREG | 0444;
		stbuf.st_nlink = 1;
		stbuf.st_size = file_size;
    }
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
}


static void UniFS_readdir(fuse_req_t req, fuse_ino_t ino, size_t size,
			     off_t off, struct fuse_file_info *fi)
{
	(void) fi;
	struct dirbuf b;

	// Find CurrDir
	std::string Folder = "";
	for(auto d : Disk.Dictionary){
		if (ino == d.ino){
			// Found
			Folder = d.FilePath;
			std::cout << Folder << std::endl;
			break;
		}
	}
	
	std::string ParentDir = getParentDirectory(Folder);

	int parentIno = 1;
	if(ParentDir.size() > 0){
		for(auto d : Disk.Dictionary){
			if(d.FilePath.size() != ParentDir.size()){
				continue;
			}

			if (!memcmp(d.FilePath.data(), ParentDir.data(), ParentDir.size())){
				// Found
				parentIno = d.ino;
				break;
			}
		}

		std::cout << parentIno << std::endl;
	}

	memset(&b, 0, sizeof(b));
	dirbuf_add(req, &b, ".", ino, true, 0);
	dirbuf_add(req, &b, "..", parentIno, true, 0);

	std::cout << ino << " " << size << " " << off << std::endl;

	// What is contained within this dir
	for(auto d : Disk.Dictionary){
		if(d.pathSize <= Folder.size()){
		// std::cout << d.FilePath << std::endl;
			continue;
		}

		if(!beginsWithAndEqualSlashes(d.FilePath, Folder)){
		// std::cout << d.FilePath << std::endl;
			continue;
		}

		std::cout << d.FilePath << std::endl;
		dirbuf_add(req, &b, d.FilePath.substr(Folder.size()).data(), d.ino, !d.type, d.DataSize);
	}

	std::cout << b.size << std::endl;

	reply_buf_limited(req, b.p, b.size, off, size);
	free(b.p);
}

static void UniFS_open(fuse_req_t req, fuse_ino_t ino,
			  struct fuse_file_info *fi)
{
	// if (ino != 2)
	// 	fuse_reply_err(req, EISDIR);
	// else if ((fi->flags & 3) != O_RDONLY)
	// 	fuse_reply_err(req, EACCES);
	// else
	// 	fuse_reply_open(req, fi);

	for(auto d : Disk.Dictionary){
		if(d.ino == ino){
			if(!d.type){
				fuse_reply_err(req, EISDIR);
				return;
			}

			fuse_reply_open(req, fi);
			return;
		}
	}

	fuse_reply_err(req, ENOENT);
	return;
}

static void UniFS_read(fuse_req_t req, fuse_ino_t ino, size_t size,
			  off_t off, struct fuse_file_info *fi)
{
	(void) fi;

	for(auto d : Disk.Dictionary){
		if(d.ino == ino){
			if(!d.type){
				fuse_reply_err(req, EISDIR);
				return;
			}

			reply_buf_limited(req, DATA + Disk.Header.DictionaryEnd + d.DataOffset, d.DataSize, off, size);
			return;
		}
	}

	fuse_reply_err(req, ENOENT);

	// std::cout << "STUB" << std::endl;
}

static void UniFS_write(fuse_req_t req, fuse_ino_t ino, const char* Data, size_t size,
			  off_t off, struct fuse_file_info *fi)
{
	// (void) fi;

	// assert(ino == 2);
	// reply_buf_limited(req, clock_str, strlen(clock_str), off, size);
}

static struct fuse_lowlevel_ops UniFS_oper = {
	.lookup		= UniFS_lookup,
	.getattr	= UniFS_getattr,
	.readdir	= UniFS_readdir,
	.open		= UniFS_open,
	.read		= UniFS_read,
	// .write		= UniFS_write,
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
		mountpoint[strlen(mountpoint) - 5] = 0;

		// Load ramdisk
		Ramdisk = fopen(FilePath, "r");
		long fileSize = 0;
		fseek(Ramdisk, 0, SEEK_END);
		fileSize = ftell(Ramdisk);
		rewind(Ramdisk);
		DATA = (char*)malloc(fileSize);
		fread(DATA, fileSize, 1, Ramdisk);
		fclose(Ramdisk);
		
		// Validate
		if(memcmp(DATA, "UNIFIED", 8)){
			fclose(Ramdisk);
			std::cout << "Failed to Validate Ramdisk" << std::endl;
			return 1;
		}

		Disk.Header = *((FileHeader*)(DATA));

		// Add Root Ino
		Disk.Dictionary.push_back({0, 0, 0, "/", 0, 0, 1});

		// Load the Dictionary
		if(Disk.Header.DictionarySize){
			// Load the dictionary
			// Dictionary Data
			char* DictionaryData = ((char*)DATA) + Disk.Header.DictionaryOffset;
			int ino = 1;

			while ((uint64_t)(DictionaryData - ((char*)DATA)) < Disk.Header.DictionaryEnd){
				// Get Size
				uint32_t Size = ((uint32_t*)(DictionaryData))[0];
				uint32_t PathSize = ((uint32_t*)(DictionaryData))[1];
				uint8_t Type = DictionaryData[8];
				DictionaryData+=9;

				// Load Const char
				char* LoadedPath = new char[PathSize + 1];
				LoadedPath[PathSize] = 0;
				memcpy(LoadedPath, DictionaryData, PathSize);
				DictionaryData += PathSize;

				uint64_t DataOffset = ((uint64_t*)(DictionaryData))[0];
				uint64_t DataSize = ((uint64_t*)(DictionaryData))[1];
				DictionaryData+=16;

				// Load string
				Disk.Dictionary.push_back({Size, PathSize, Type, LoadedPath, DataOffset, DataSize, ino++});
			}
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

	return err ? 1 : 0;
}