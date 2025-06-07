#include "hash-table.h"
#include "murmur3.h"
#include "trace.h"

#include <algorithm>
#include <asm-generic/errno-base.h>
#include <cstdint>
#include <fcntl.h>
#include <linked-list.h>
#include <malloc.h>

#define FUSE_USE_VERSION 30
#include <fuse.h>

#include <stdio.h>
#include <string.h>
#include <string.h>
#include <unistd.h>

#include <unordered_set>
#include <unordered_map>
#include <string>
#include <vector>

#include <assert.h>


const uint32_t HASH_SEED = 42;
const size_t HASH_SIZE_IN_32BIT_CHUNKS = 128 / 32;

struct hash_t {
    uint32_t data[HASH_SIZE_IN_32BIT_CHUNKS];
};


const size_t BLOCK_SIZE = 32;

struct block {
    char data[BLOCK_SIZE];
    size_t size;
};


typedef element_index_t block_id_t;

struct block_storage {
    hash_table<hash_t, element_index_t> block_map;
    linked_list<block> allocator;

    block_storage(): block_map {}, allocator {} {
        hash_table_create(&block_map, hash_hash, 32, 10, hash_equal);
        linked_list_create(&allocator);
    }

    ~block_storage() {
        hash_table_destroy(&block_map);
        linked_list_destroy(&allocator);
    }

    static uint32_t hash_hash(hash_t hash) { return hash.data[0]; }
    static bool hash_equal(hash_t* first, hash_t* second) {
        for (int i = 0; i < HASH_SIZE_IN_32BIT_CHUNKS; ++ i)
            if (first->data[i] != second->data[i])
                return false;

        return true;
    }

    block_id_t get_block(const char* data, size_t size) {
        // Try to find existing block
        block_id_t found_block = find_block(data, size);
        if (found_block != linked_list_end_index)
            return found_block;

        block new_block {};
        new_block.size = size;

        std::copy(data, data + size, new_block.data);

        // Allocate new block
        TRY linked_list_push_front(&allocator, new_block)
            THROW("Fail!");

        element_index_t newly_added =
            linked_list_head_index(&allocator);

        // Register it in map
        hash_t block_hash;
        murmur3_x64_128(data, size, HASH_SEED, &block_hash);

        hash_table_insert<hash_t, element_index_t>(&block_map, block_hash, newly_added);

        // Return newly created block
        return linked_list_head_index(&allocator);
    }

    block_id_t find_block(const char* data, size_t size) {
        hash_t block_hash;
        murmur3_x64_128(data, size, HASH_SEED, &block_hash);

        element_index_t *found_block_index =
            hash_table_lookup<hash_t, element_index_t>(&block_map, block_hash);

        if (!found_block_index)
            return linked_list_end_index;

        return *found_block_index;
    }

    block* get_block(block_id_t block_id) {
        return &linked_list_get_pointer(&allocator, block_id)->element;
        // TODO: unsafe
    }
};


const size_t MAX_FILE_NAME = 128;

struct file {
    char name[MAX_FILE_NAME];
    linked_list<block_id_t> block_chain;

    size_t size;
};

void file_create(file* target_file) {
    linked_list_create(&target_file->block_chain);
}


struct file_storage {
    block_storage blocks;
    linked_list<file> files;
};

void file_storage_create(file_storage* storage) {
    linked_list_create(&storage->files);
}

void dump_files(file_storage* storage) {
    printf("================> FILE DUMP:\n");

    LINKED_LIST_TRAVERSE(&storage->files, file, current) {
        file *current_file = &current->element;

        printf("file: \"%s\" (%zu bytes) => ", current_file->name, current_file->size);

        LINKED_LIST_TRAVERSE(&current_file->block_chain, block_id_t, current) {
            block* current_block = storage->blocks.get_block(current->element);

            printf("{ %p %zu } ", current_block, current_block->size);
        }

        printf("\n");
    }

    printf("============================\n");
}


file* file_storage_find_file(file_storage* storage, const char* name) {
    printf("  find file: %s\n", name);

    LINKED_LIST_TRAVERSE(&storage->files, file, current) {
        file* file_ptr = &current->element;

        if (strcmp(file_ptr->name, name) == 0)
            return file_ptr;
    }

    return nullptr;
}

file* file_storage_add_file(file_storage* storage, const char* name) {
    file new_file {}; // TODO: improve, slow!

    strncpy(new_file.name, name, MAX_FILE_NAME);
    file_create(&new_file);

    linked_list_push_front(&storage->files, new_file);
    return &linked_list_head(&storage->files)->element;
}

void file_write(file_storage* storage, const char* data, size_t size, file* file) {
    assert(file && storage);

    file->size += size;

    size_t number_of_whole_blocks = size / BLOCK_SIZE;
    for (int i = 0; i < number_of_whole_blocks; ++ i) {
        block_id_t new_block = storage->blocks.get_block(data, BLOCK_SIZE);
        linked_list_push_back(&file->block_chain, new_block);

        data += BLOCK_SIZE;
    }

    size %= BLOCK_SIZE;
    if (size != 0) { // TODO: write incomplete block
        block_id_t new_block = storage->blocks.get_block(data, size);
        linked_list_push_back(&file->block_chain, new_block);
    }
}

static file_storage storage;

// static void read_and_shift(char** dest, const char* src, size_t offset, size_t size) {
//     memcpy(*dest, src + offset, size);
// }


static int do_read(const char *path, char *buffer, size_t size, off_t offset, fuse_file_info*) {
    printf("do_read: %s, size: %zu\n", path, size);

    file* target_file = file_storage_find_file(&storage, path + 1);
    // TODO:                                                  ^~~ generalize
    if (!target_file)
        return -1; // Not found!

    // Calculate number of bytes we're going to read:
    size_t gonna_read = std::min(size, target_file->size - offset);

    size = gonna_read;
    printf("gonna read: %zu\n", size);

    linked_list<block_id_t> *blocks = &target_file->block_chain;
    linked_list_linearize(blocks); // Prepare for continuous access

    // ======> Get first block to read:
    element_index_t first_block_to_read = offset / BLOCK_SIZE;

    element_index_t first_block_index;
    TRY linked_list_get_logical_index(blocks, first_block_to_read, &first_block_index)
        ASSERT_SUCCESS();

    element<block_id_t>* current = linked_list_get_pointer(blocks, first_block_index);
    printf("reading block: %d\n", current->element);

    // Keep track of read bytes:
    size_t read_bytes = 0;

    // ======> Read first block's leftovers:
    size_t offset_in_1st_block = offset % BLOCK_SIZE;
    size_t bytes_read_1st_block = std::min(BLOCK_SIZE - offset_in_1st_block, size);

    memcpy(buffer, storage.blocks.get_block(current->element)->data + offset_in_1st_block, bytes_read_1st_block);
    buffer += bytes_read_1st_block; // TODO: extract
    size   -= bytes_read_1st_block;

    printf("left after first block: %zu\n", size);

    // ======> Read completed blocks:

    // Write full blocks:
    size_t full_blocks_remaining = size / BLOCK_SIZE;

    for (int i = 0; i < full_blocks_remaining; ++ i) {
        current = linked_list_next(blocks, current);

        memcpy(buffer, storage.blocks.get_block(current->element)->data, BLOCK_SIZE);
        buffer += BLOCK_SIZE;

        printf("%d block: %zu\n", i, size);
    }

    size %= BLOCK_SIZE; // Leftovers

    printf("last block: %zu\n", size);

    if (size != 0) {
        // TODO: extract
        current = linked_list_next(blocks, current);
        memcpy(buffer, storage.blocks.get_block(current->element)->data, size);
    }

    return gonna_read;
}

static int do_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    printf("do_readdir: %s\n", path);
    
	filler(buffer,  ".", NULL, 0); // Current Directory
	filler(buffer, "..", NULL, 0); // Parent Directory
	
    // If the user is trying to show the files/directories of the root directory show the following
	if (strcmp(path, "/") == 0) {
        LINKED_LIST_TRAVERSE(&storage.files, file, current_file) {
            filler(buffer, current_file->element.name, NULL, 0);
        }
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
	} else if (file* my_file = file_storage_find_file(&storage, path + 1)) {
		st->st_mode = S_IFREG | 0644;
		st->st_nlink = 1;
		st->st_size = my_file->size;
	} else {
        printf("(%d)\n", -ENOENT);
        return -ENOENT;
    }
	
    printf("(0)\n");
    return 0;
}

static int do_mknod(const char* path, mode_t mode, dev_t dev) {
    printf("do_mknode: %s\n", path);

    // TODO: path optimization
    file_storage_add_file(&storage, path + 1);
    return 0;
}

static int do_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
    printf("do_write: %s\n", path);

    file* target_file = file_storage_find_file(&storage, path + 1);
    if (!target_file)
        return -1;

    file_write(&storage, buffer, size, target_file);

    dump_files(&storage);
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


void block_tests() {
    char block[BLOCK_SIZE];

    block_storage blocks;
    printf("%d\n", blocks.get_block(block, BLOCK_SIZE));
    printf("%d\n", blocks.get_block(block, BLOCK_SIZE));
    block[10] = 1;
    printf("%d\n", blocks.get_block(block, BLOCK_SIZE));
    printf("%d\n", blocks.get_block(block, BLOCK_SIZE));

    char other[BLOCK_SIZE];
    printf("%d\n", blocks.get_block(other, BLOCK_SIZE));
    printf("%d\n", blocks.get_block(other, BLOCK_SIZE));

    printf("%d\n", blocks.get_block(block, BLOCK_SIZE));
}

int main(int argc, char* argv[]) {
    file_storage_create(&storage);

    setvbuf(stdout, NULL, _IONBF, 0);
    return fuse_main(argc, argv, &dedfs_operations, NULL);
}
