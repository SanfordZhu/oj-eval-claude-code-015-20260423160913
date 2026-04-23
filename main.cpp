#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <sys/stat.h>

using namespace std;

const char* DB_FILE = "database.dat";
const int NUM_BUCKETS = 10007;  // Prime number for hashing
const int BLOCK_SIZE = 512;  // Fixed block size

// Fixed-size block stored on disk
struct Block {
    int pos;  // File position
    int next_block_pos;  // Position of next block in chain (-1 if none)
    int num_entries;

    struct Entry {
        bool deleted;
        string key;
        int value;

        int size() const {
            return 1 + 1 + key.size() + 4;  // deleted + key_len + key + value
        }
    };

    vector<Entry> entries;

    Block() : pos(-1), next_block_pos(-1), num_entries(0) {}

    int data_size() const {
        int sz = 0;
        for (const auto& e : entries) {
            sz += e.size();
        }
        return sz;
    }
};

fstream db_file;
int bucket_pos[NUM_BUCKETS];  // Position of first block for each bucket
int next_pos = 0;

void open_db() {
    struct stat buffer;
    bool exists = (stat(DB_FILE, &buffer) == 0);

    if (exists) {
        db_file.open(DB_FILE, ios::binary | ios::in | ios::out);
        db_file.seekg(0);
        db_file.read(reinterpret_cast<char*>(bucket_pos), NUM_BUCKETS * 4);
        db_file.read(reinterpret_cast<char*>(&next_pos), 4);
    } else {
        // Create new file
        db_file.open(DB_FILE, ios::binary | ios::out | ios::trunc);
        memset(bucket_pos, -1, sizeof(bucket_pos));
        next_pos = NUM_BUCKETS * 4 + 4;  // Start after header
        db_file.write(reinterpret_cast<char*>(bucket_pos), NUM_BUCKETS * 4);
        db_file.write(reinterpret_cast<char*>(&next_pos), 4);
        db_file.close();
        // Reopen for read/write
        db_file.open(DB_FILE, ios::binary | ios::in | ios::out);
    }
}

void close_db() {
    db_file.clear();
    db_file.seekp(0);
    db_file.write(reinterpret_cast<char*>(bucket_pos), NUM_BUCKETS * 4);
    db_file.write(reinterpret_cast<char*>(&next_pos), 4);
    db_file.flush();
    db_file.close();
}

Block* read_block(int pos) {
    if (pos < 0) return nullptr;
    Block* block = new Block();
    block->pos = pos;
    db_file.clear();
    db_file.seekg(pos);

    db_file.read(reinterpret_cast<char*>(&block->next_block_pos), 4);
    db_file.read(reinterpret_cast<char*>(&block->num_entries), 4);

    for (int i = 0; i < block->num_entries; i++) {
        Block::Entry e;
        uint8_t deleted, key_len;
        db_file.read(reinterpret_cast<char*>(&deleted), 1);
        db_file.read(reinterpret_cast<char*>(&key_len), 1);
        e.deleted = (deleted != 0);
        e.key.resize(key_len);
        db_file.read(&e.key[0], key_len);
        db_file.read(reinterpret_cast<char*>(&e.value), 4);
        block->entries.push_back(e);
    }

    return block;
}

void write_block(Block* block) {
    int pos = block->pos;

    db_file.clear();
    db_file.seekp(pos);

    db_file.write(reinterpret_cast<char*>(&block->next_block_pos), 4);
    db_file.write(reinterpret_cast<char*>(&block->num_entries), 4);

    for (const auto& e : block->entries) {
        uint8_t deleted = e.deleted ? 1 : 0;
        uint8_t key_len = e.key.size();
        db_file.write(reinterpret_cast<char*>(&deleted), 1);
        db_file.write(reinterpret_cast<char*>(&key_len), 1);
        db_file.write(e.key.c_str(), key_len);
        db_file.write(reinterpret_cast<const char*>(&e.value), 4);
    }
    db_file.flush();
}

unsigned int hash_key(const string& key) {
    unsigned int h = 5381;
    for (char c : key) {
        h = ((h << 5) + h) + c;
    }
    return h % NUM_BUCKETS;
}

void insert(const string& key, int value) {
    unsigned int bucket = hash_key(key);

    // Check if entry already exists
    int pos = bucket_pos[bucket];
    while (pos >= 0) {
        Block* block = read_block(pos);
        for (auto& e : block->entries) {
            if (!e.deleted && e.key == key && e.value == value) {
                delete block;
                return;  // Already exists
            }
        }
        pos = block->next_block_pos;
        delete block;
    }

    // Find a block with space or create new one
    pos = bucket_pos[bucket];
    Block* last_block = nullptr;
    int last_pos = -1;

    while (pos >= 0) {
        Block* block = read_block(pos);
        int header_size = 8;  // next_block_pos + num_entries
        int new_entry_size = 1 + 1 + key.size() + 4;  // deleted + key_len + key + value
        if (block->data_size() + new_entry_size + header_size <= BLOCK_SIZE) {
            // Has space
            Block::Entry e;
            e.deleted = false;
            e.key = key;
            e.value = value;
            block->entries.push_back(e);
            block->num_entries++;
            write_block(block);
            delete block;
            return;
        }
        last_pos = pos;
        pos = block->next_block_pos;
        delete last_block;
        last_block = block;
    }

    // Need new block
    Block* new_block = new Block();
    new_block->pos = next_pos;
    next_pos += BLOCK_SIZE;
    Block::Entry e;
    e.deleted = false;
    e.key = key;
    e.value = value;
    new_block->entries.push_back(e);
    new_block->num_entries = 1;
    write_block(new_block);

    if (last_block) {
        last_block->next_block_pos = new_block->pos;
        write_block(last_block);
        delete last_block;
    } else {
        bucket_pos[bucket] = new_block->pos;
        // Write bucket positions back to header
        db_file.clear();
        db_file.seekp(bucket * 4);
        db_file.write(reinterpret_cast<char*>(&bucket_pos[bucket]), 4);
        db_file.flush();
    }
    delete new_block;
}

void delete_entry(const string& key, int value) {
    unsigned int bucket = hash_key(key);
    int pos = bucket_pos[bucket];

    while (pos >= 0) {
        Block* block = read_block(pos);
        for (auto& e : block->entries) {
            if (!e.deleted && e.key == key && e.value == value) {
                e.deleted = true;
                write_block(block);
                delete block;
                return;
            }
        }
        pos = block->next_block_pos;
        delete block;
    }
}

void find(const string& key) {
    unsigned int bucket = hash_key(key);
    int pos = bucket_pos[bucket];
    vector<int> values;

    while (pos >= 0) {
        Block* block = read_block(pos);
        for (const auto& e : block->entries) {
            if (!e.deleted && e.key == key) {
                values.push_back(e.value);
            }
        }
        pos = block->next_block_pos;
        delete block;
    }

    if (values.empty()) {
        cout << "null" << endl;
        return;
    }

    sort(values.begin(), values.end());
    values.erase(unique(values.begin(), values.end()), values.end());

    for (size_t i = 0; i < values.size(); i++) {
        if (i > 0) cout << " ";
        cout << values[i];
    }
    cout << endl;
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    open_db();

    int n;
    cin >> n;

    for (int i = 0; i < n; i++) {
        string cmd;
        cin >> cmd;

        if (cmd == "insert") {
            string key;
            int value;
            cin >> key >> value;
            insert(key, value);
        } else if (cmd == "delete") {
            string key;
            int value;
            cin >> key >> value;
            delete_entry(key, value);
        } else if (cmd == "find") {
            string key;
            cin >> key;
            find(key);
        }
    }

    close_db();

    return 0;
}
