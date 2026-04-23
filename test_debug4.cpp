#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdint>

using namespace std;

const char* DB_FILE = "database.dat";
const int NUM_BUCKETS = 10007;
const int BLOCK_SIZE = 512;

fstream db_file;
int bucket_pos[NUM_BUCKETS];
int next_pos = 0;

void open_db() {
    ifstream test_file(DB_FILE, ios::binary);
    bool exists = test_file.is_open();
    test_file.close();

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
    db_file.close();
}

unsigned int hash_key(const string& key) {
    unsigned int h = 5381;
    for (char c : key) {
        h = ((h << 5) + h) + c;
    }
    return h % NUM_BUCKETS;
}

bool read_block_header(int pos, int& next_block, int& num_entries, int& data_start) {
    if (pos < 0) return false;
    db_file.clear();
    db_file.seekg(pos);
    db_file.read(reinterpret_cast<char*>(&next_block), 4);
    db_file.read(reinterpret_cast<char*>(&num_entries), 4);
    data_start = pos + 8;
    return true;
}

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

void write_block_header(int pos, int next_block, int num_entries) {
    db_file.clear();
    db_file.seekp(pos);
    db_file.write(reinterpret_cast<char*>(&next_block), 4);
    db_file.write(reinterpret_cast<char*>(&num_entries), 4);
    db_file.flush();
}

void write_entry(int pos, bool deleted, const string& key, int value) {
    db_file.clear();
    db_file.seekp(pos);
    uint8_t del = deleted ? 1 : 0;
    uint8_t key_len = key.size();
    db_file.write(reinterpret_cast<char*>(&del), 1);
    db_file.write(reinterpret_cast<char*>(&key_len), 1);
    db_file.write(key.c_str(), key_len);
    db_file.write(reinterpret_cast<char*>(&value), 4);
    db_file.flush();
}

int entry_size(const string& key) {
    return 1 + 1 + key.size() + 4;
}

void insert(const string& key, int value) {
    unsigned int bucket = hash_key(key);
    int entry_sz = entry_size(key);
    cerr << "insert: key=" << key << ", value=" << value << ", bucket=" << bucket << endl;

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
                cerr << "  entry already exists" << endl;
                return;
            }
        }
        pos = next_block;
    }

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

        cerr << "  block at pos=" << pos << ", num_entries=" << num_entries << ", data_sz=" << data_sz << endl;

        if (data_sz + entry_sz + 8 <= BLOCK_SIZE) {
            int write_pos = data_start + data_sz;
            cerr << "  appending at write_pos=" << write_pos << endl;
            write_entry(write_pos, false, key, value);
            write_block_header(pos, next_block, num_entries + 1);
            return;
        }

        last_pos = pos;
        last_next = next_block;
        pos = next_block;
    }

    int new_pos = next_pos;
    next_pos += BLOCK_SIZE;
    cerr << "  creating new block at pos=" << new_pos << endl;
    write_block_header(new_pos, -1, 1);
    write_entry(new_pos + 8, false, key, value);

    if (last_pos >= 0) {
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

void find(const string& key) {
    cerr << "find: key=" << key << endl;
    unsigned int bucket = hash_key(key);
    int pos = bucket_pos[bucket];
    vector<int> values;

    while (pos >= 0) {
        int next_block, num_entries, data_start;
        if (!read_block_header(pos, next_block, num_entries, data_start)) break;
        cerr << "  block at pos=" << pos << ", num_entries=" << num_entries << endl;

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

    cerr << "  values.size()=" << values.size() << endl;

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
    cerr << "n=" << n << endl;

    for (int i = 0; i < n; i++) {
        string cmd;
        cin >> cmd;
        cerr << "cmd=" << cmd << endl;

        if (cmd == "insert") {
            string key;
            int value;
            cin >> key >> value;
            insert(key, value);
        } else if (cmd == "find") {
            string key;
            cin >> key;
            find(key);
        }
    }

    close_db();

    return 0;
}
