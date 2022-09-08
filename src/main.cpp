#include <algorithm>
#include <asm-generic/errno-base.h>
#include <fcntl.h>

#define FUSE_USE_VERSION 30
#include <fuse.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <unordered_set>
#include <unordered_map>
#include <string>
#include <vector>


class file {
public:
    std::vector<char> data;
};

class file_storage {
public:
    file* find_file(const char *path) {
        auto found = files.find(path);
        file* result = found == files.end() ? nullptr : found->second;

        return result;
    }

    void add_file(const char* name) {
        storage.emplace_back();
        files[name] = &storage.back();
    }

    void write_file(file* file_to_write, const char* buffer, size_t size) {
        file_to_write->data.assign(buffer, buffer + size);
    }

    std::unordered_map<std::string, file*> files;

private:
    std::vector<file> storage;
};

static file_storage storage;

static int do_read(const char *path, char *buffer, size_t size, off_t offset, fuse_file_info *fi) {
    printf("do_read: %s\n", path);

    const file* target_file = storage.find_file(path + 1);
    if (!target_file)
        return -1;

    const auto& data = target_file->data;

    memcpy(buffer, &*data.begin(), size);
    return data.size() - offset;
}

static int do_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    printf("do_readdir: %s\n", path);
    
	filler(buffer,  ".", NULL, 0); // Current Directory
	filler(buffer, "..", NULL, 0); // Parent Directory
	
    // If the user is trying to show the files/directories of the root directory show the following
	if (strcmp(path, "/") == 0) {
        for (const auto& [name, _]: storage.files)
            filler(buffer, name.c_str(), NULL, 0);
	}
	
    return 0;
}

static int do_getattr(const char *path, struct stat *st) {
    printf("do_getattr: %s ", path);

	st->st_uid = getuid(); // The owner of the file/directory is the user who mounted the filesystem
	st->st_gid = getgid(); // The group of the file/directory is the same as the group of the user who mounted the filesystem
	st->st_atime = time(NULL); // The last "a"ccess of the file/directory is right now
	st->st_mtime = time(NULL); // The last "m"odification of the file/directory is right now
	
	if ( strcmp( path, "/" ) == 0 && path[1] == '\0' ) {
		st->st_mode = S_IFDIR | 0755;
		st->st_nlink = 2; // Why "two" hardlinks instead of "one"? The answer is here: http://unix.stackexchange.com/a/101536
	} else if (storage.find_file(path + 1)) {
		st->st_mode = S_IFREG | 0644;
		st->st_nlink = 1;
		st->st_size = 1024;
	} else {
        printf("(%d)\n", -ENOENT);
        return -ENOENT;
    }
	
    printf("(0)\n");
    return 0;
}

static int do_mknod(const char* path, mode_t mode, dev_t dev) {
    printf("do_mknode: %s\n", path);

    storage.add_file(path + 1);
    return 0;
}

static int do_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
    printf("do_write: %s\n", path);

    file* target_file = storage.find_file(path + 1);
    if (!target_file)
        return -1;

    storage.write_file(target_file, buffer, size);
    return size;
}

static int do_mkdir( const char *path, mode_t mode ) {
    printf("do_mkdir: %s\n", path);
	return 0;
}


static const fuse_operations dedfs_operations = {
    .getattr	= do_getattr,
    .mknod		= do_mknod,
    .mkdir		= do_mkdir,
    .read		= do_read,
    .write		= do_write,
    .readdir	= do_readdir,
};


int main(int argc, char* argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);
    return fuse_main(argc, argv, &dedfs_operations, NULL);
}
