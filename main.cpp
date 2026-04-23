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
const int NUM_BUCKETS = 10007;

fstream db_file;
int bucket_start[NUM_BUCKETS];  // Starting position of each bucket
int bucket_end[NUM_BUCKETS];    // Ending position of each bucket

void open_db() {
    ifstream test_file(DB_FILE, ios::binary);
    bool exists = test_file.is_open();
    test_file.close();

    if (exists) {
        db_file.open(DB_FILE, ios::binary | ios::in | ios::out);
        db_file.seekg(0);
        db_file.read(reinterpret_cast<char*>(bucket_start), NUM_BUCKETS * 4);
        db_file.read(reinterpret_cast<char*>(bucket_end), NUM_BUCKETS * 4);
    } else {
        db_file.open(DB_FILE, ios::binary | ios::out | ios::trunc);
        memset(bucket_start, 0, sizeof(bucket_start));
        memset(bucket_end, 0, sizeof(bucket_end));
    }
}

void close_db() {
    db_file.clear();
    db_file.seekp(0);
    db_file.write(reinterpret_cast<char*>(bucket_start), NUM_BUCKETS * 4);
    db_file.write(reinterpret_cast<char*>(bucket_end), NUM_BUCKETS * 4);
    db_file.close();
}

unsigned int hash_key(const string& key) {
    unsigned int h = 5381;
    for (char c : key) {
        h = ((h << 5) + h) + c;
    }
    return h % NUM_BUCKETS;
}

// Record format: deleted(1) + key_len(1) + key(16) + value(4)
int entry_size(const string& key) {
    return 1 + 1 + key.size() + 4;
}

void insert(const string& key, int value) {
    unsigned int bucket = hash_key(key);
    int sz = entry_size(key);

    // Check if entry already exists
    int pos = bucket_start[bucket];
    int end_pos = bucket_end[bucket];
    while (pos < end_pos) {
        uint8_t deleted;
        uint8_t key_len;
        db_file.clear();
        db_file.seekg(pos);
        db_file.read(reinterpret_cast<char*>(&deleted), 1);
        db_file.read(reinterpret_cast<char*>(&key_len), 1);
        string k(key_len, '\0');
        if (key_len > 0) {
            db_file.read(&k[0], key_len);
        }
        int v;
        db_file.read(reinterpret_cast<char*>(&v), 4);

        if (!deleted && k == key && v == value) {
            return;  // Already exists
        }
        pos += sz;
    }

    // Append new entry
    pos = bucket_end[bucket];
    db_file.clear();
    db_file.seekp(pos);
    uint8_t deleted = 0;
    uint8_t key_len = key.size();
    db_file.write(reinterpret_cast<char*>(&deleted), 1);
    db_file.write(reinterpret_cast<char*>(&key_len), 1);
    db_file.write(key.c_str(), key_len);
    db_file.write(reinterpret_cast<const char*>(&value), 4);

    bucket_end[bucket] += sz;
}

void delete_entry(const string& key, int value) {
    unsigned int bucket = hash_key(key);
    int sz = entry_size(key);

    int pos = bucket_start[bucket];
    int end_pos = bucket_end[bucket];
    while (pos < end_pos) {
        uint8_t deleted;
        uint8_t key_len;
        db_file.clear();
        db_file.seekg(pos);
        db_file.read(reinterpret_cast<char*>(&deleted), 1);
        db_file.read(reinterpret_cast<char*>(&key_len), 1);
        string k(key_len, '\0');
        if (key_len > 0) {
            db_file.read(&k[0], key_len);
        }
        int v;
        db_file.read(reinterpret_cast<char*>(&v), 4);

        if (!deleted && k == key && v == value) {
            // Mark as deleted
            db_file.clear();
            db_file.seekp(pos);
            deleted = 1;
            db_file.write(reinterpret_cast<char*>(&deleted), 1);
            db_file.flush();
            return;
        }
        pos += sz;
    }
}

void find(const string& key) {
    unsigned int bucket = hash_key(key);
    int sz = entry_size(key);

    int pos = bucket_start[bucket];
    int end_pos = bucket_end[bucket];
    vector<int> values;

    while (pos < end_pos) {
        uint8_t deleted;
        uint8_t key_len;
        db_file.clear();
        db_file.seekg(pos);
        db_file.read(reinterpret_cast<char*>(&deleted), 1);
        db_file.read(reinterpret_cast<char*>(&key_len), 1);
        string k(key_len, '\0');
        if (key_len > 0) {
            db_file.read(&k[0], key_len);
        }
        int v;
        db_file.read(reinterpret_cast<char*>(&v), 4);

        if (!deleted && k == key) {
            values.push_back(v);
        }
        pos += sz;
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
