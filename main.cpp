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
        db_file.open(DB_FILE, ios::binary | ios::out | ios::trunc);
        memset(bucket_pos, -1, sizeof(bucket_pos));
        next_pos = NUM_BUCKETS * 4 + 4;
        db_file.write(reinterpret_cast<char*>(bucket_pos), NUM_BUCKETS * 4);
        db_file.write(reinterpret_cast<char*>(&next_pos), 4);
        db_file.close();
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

unsigned int hash_key(const string& key) {
    unsigned int h = 5381;
    for (char c : key) {
        h = ((h << 5) + h) + c;
    }
    return h % NUM_BUCKETS;
}

// Read block header and return data position and next block pos
bool read_block_header(int pos, int& next_block, int& num_entries, int& data_start) {
    if (pos < 0) return false;
    db_file.clear();
    db_file.seekg(pos);
    db_file.read(reinterpret_cast<char*>(&next_block), 4);
    db_file.read(reinterpret_cast<char*>(&num_entries), 4);
    data_start = pos + 8;
    return true;
}

// Read a single entry from data position
bool read_entry(int& pos, bool& deleted, string& key, int& value) {
    uint8_t del, key_len;
    if (!db_file.read(reinterpret_cast<char*>(&del), 1)) return false;
    if (!db_file.read(reinterpret_cast<char*>(&key_len), 1)) return false;
    deleted = (del != 0);
    key.resize(key_len);
    if (key_len > 0 && !db_file.read(&key[0], key_len)) return false;
    if (!db_file.read(reinterpret_cast<char*>(&value), 4)) return false;
    pos += 1 + 1 + key_len + 4;
    return true;
}

// Write block header
void write_block_header(int pos, int next_block, int num_entries) {
    db_file.clear();
    db_file.seekp(pos);
    db_file.write(reinterpret_cast<char*>(&next_block), 4);
    db_file.write(reinterpret_cast<char*>(&num_entries), 4);
}

// Write an entry at position
int write_entry(int pos, bool deleted, const string& key, int value) {
    db_file.clear();
    db_file.seekp(pos);
    uint8_t del = deleted ? 1 : 0;
    uint8_t key_len = key.size();
    db_file.write(reinterpret_cast<char*>(&del), 1);
    db_file.write(reinterpret_cast<char*>(&key_len), 1);
    db_file.write(key.c_str(), key_len);
    db_file.write(reinterpret_cast<char*>(&value), 4);
    db_file.flush();
    return pos + 1 + 1 + key_len + 4;
}

// Calculate entry size
int entry_size(const string& key) {
    return 1 + 1 + key.size() + 4;  // deleted + key_len + key + value
}

void insert(const string& key, int value) {
    unsigned int bucket = hash_key(key);
    int entry_sz = entry_size(key);

    // Check if entry already exists
    int pos = bucket_pos[bucket];
    while (pos >= 0) {
        int next_block, num_entries, data_start;
        if (!read_block_header(pos, next_block, num_entries, data_start)) break;

        int read_pos = data_start;
        for (int i = 0; i < num_entries; i++) {
            bool deleted;
            string k;
            int v;
            if (!read_entry(read_pos, deleted, k, v)) break;
            if (!deleted && k == key && v == value) {
                return;  // Already exists
            }
        }
        pos = next_block;
    }

    // Find a block with space or create new one
    pos = bucket_pos[bucket];
    int last_pos = -1;
    int last_next = -1;

    while (pos >= 0) {
        int next_block, num_entries, data_start;
        if (!read_block_header(pos, next_block, num_entries, data_start)) break;

        int data_sz = 0;
        int read_pos = data_start;
        for (int i = 0; i < num_entries; i++) {
            bool deleted;
            string k;
            int v;
            if (!read_entry(read_pos, deleted, k, v)) break;
            data_sz += entry_size(k);
        }

        if (data_sz + entry_sz + 8 <= BLOCK_SIZE) {
            // Has space - append entry
            int write_pos = data_start + data_sz;
            write_entry(write_pos, false, key, value);
            write_block_header(pos, next_block, num_entries + 1);
            return;
        }

        last_pos = pos;
        last_next = next_block;
        pos = next_block;
    }

    // Need new block
    int new_pos = next_pos;
    next_pos += BLOCK_SIZE;
    write_block_header(new_pos, -1, 1);
    write_entry(new_pos + 8, false, key, value);

    if (last_pos >= 0) {
        write_block_header(last_pos, new_pos, 0);  // Will update num_entries below
        // Need to read and rewrite header with correct num_entries
        int next_block, num_entries, data_start;
        read_block_header(last_pos, next_block, num_entries, data_start);
        write_block_header(last_pos, new_pos, num_entries);
    } else {
        bucket_pos[bucket] = new_pos;
        db_file.clear();
        db_file.seekp(bucket * 4);
        db_file.write(reinterpret_cast<char*>(&bucket_pos[bucket]), 4);
        db_file.flush();
    }
}

void delete_entry(const string& key, int value) {
    unsigned int bucket = hash_key(key);
    int pos = bucket_pos[bucket];

    while (pos >= 0) {
        int next_block, num_entries, data_start;
        if (!read_block_header(pos, next_block, num_entries, data_start)) break;

        int read_pos = data_start;
        for (int i = 0; i < num_entries; i++) {
            bool deleted;
            string k;
            int v;
            int entry_start = read_pos;
            if (!read_entry(read_pos, deleted, k, v)) break;
            if (!deleted && k == key && v == value) {
                // Mark as deleted
                db_file.clear();
                db_file.seekp(entry_start);
                uint8_t del = 1;
                db_file.write(reinterpret_cast<char*>(&del), 1);
                db_file.flush();
                return;
            }
        }
        pos = next_block;
    }
}

void find(const string& key) {
    unsigned int bucket = hash_key(key);
    int pos = bucket_pos[bucket];
    vector<int> values;

    while (pos >= 0) {
        int next_block, num_entries, data_start;
        if (!read_block_header(pos, next_block, num_entries, data_start)) break;

        int read_pos = data_start;
        for (int i = 0; i < num_entries; i++) {
            bool deleted;
            string k;
            int v;
            if (!read_entry(read_pos, deleted, k, v)) break;
            if (!deleted && k == key) {
                values.push_back(v);
            }
        }
        pos = next_block;
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
