#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cstring>
#include <cstdint>

using namespace std;

const char* DB_FILE = "database.dat";

struct Entry {
    bool deleted;
    string index;
    int value;
    uint64_t pos;  // file position
};

// Global hash map: index -> list of file positions
unordered_map<string, vector<uint64_t>> index_map;

void load_database() {
    ifstream file(DB_FILE, ios::binary);
    if (!file.is_open()) return;

    file.seekg(0, ios::end);
    uint64_t file_size = file.tellg();
    file.seekg(0, ios::beg);

    while (file.tellg() < file_size) {
        uint64_t pos = file.tellg();

        uint8_t deleted;
        file.read(reinterpret_cast<char*>(&deleted), 1);

        uint8_t index_len;
        file.read(reinterpret_cast<char*>(&index_len), 1);

        string index_str(index_len, '\0');
        file.read(&index_str[0], index_len);

        int value;
        file.read(reinterpret_cast<char*>(&value), 4);

        // Only add to map if not deleted
        if (!deleted) {
            index_map[index_str].push_back(pos);
        }
    }
}

void write_entry(ofstream& file, const string& index, int value, bool deleted = false) {
    uint8_t del = deleted ? 1 : 0;
    uint8_t index_len = index.size();

    file.write(reinterpret_cast<const char*>(&del), 1);
    file.write(reinterpret_cast<const char*>(&index_len), 1);
    file.write(index.c_str(), index_len);
    file.write(reinterpret_cast<const char*>(&value), 4);
}

void insert(const string& index, int value) {
    ofstream file(DB_FILE, ios::binary | ios::app);
    if (!file.is_open()) {
        file.open(DB_FILE, ios::binary | ios::out);
    }

    uint64_t pos = file.tellp();
    write_entry(file, index, value, false);
    file.close();

    index_map[index].push_back(pos);
}

void delete_entry(const string& index, int value) {
    auto it = index_map.find(index);
    if (it == index_map.end()) return;

    // Read entries and find matching one
    ifstream file(DB_FILE, ios::binary);
    if (!file.is_open()) return;

    for (uint64_t pos : it->second) {
        file.seekg(pos);

        uint8_t deleted;
        file.read(reinterpret_cast<char*>(&deleted), 1);
        if (deleted) continue;

        uint8_t index_len;
        file.read(reinterpret_cast<char*>(&index_len), 1);

        string stored_index(index_len, '\0');
        file.read(&stored_index[0], index_len);

        int stored_value;
        file.read(reinterpret_cast<char*>(&stored_value), 4);

        if (stored_value == value) {
            // Mark as deleted
            file.close();

            fstream f(DB_FILE, ios::binary | ios::in | ios::out);
            f.seekp(pos);
            uint8_t del = 1;
            f.write(reinterpret_cast<const char*>(&del), 1);
            f.close();
            return;
        }
    }
}

void find(const string& index) {
    auto it = index_map.find(index);
    if (it == index_map.end() || it->second.empty()) {
        cout << "null" << endl;
        return;
    }

    vector<int> values;
    ifstream file(DB_FILE, ios::binary);
    if (!file.is_open()) {
        cout << "null" << endl;
        return;
    }

    for (uint64_t pos : it->second) {
        file.seekg(pos);

        uint8_t deleted;
        file.read(reinterpret_cast<char*>(&deleted), 1);
        if (deleted) continue;

        uint8_t index_len;
        file.read(reinterpret_cast<char*>(&index_len), 1);

        string stored_index(index_len, '\0');
        file.read(&stored_index[0], index_len);

        int value;
        file.read(reinterpret_cast<char*>(&value), 4);

        values.push_back(value);
    }

    file.close();

    if (values.empty()) {
        cout << "null" << endl;
        return;
    }

    sort(values.begin(), values.end());

    // Remove duplicates (shouldn't happen per problem spec, but just in case)
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

    load_database();

    int n;
    cin >> n;

    for (int i = 0; i < n; i++) {
        string cmd;
        cin >> cmd;

        if (cmd == "insert") {
            string index;
            int value;
            cin >> index >> value;
            insert(index, value);
        } else if (cmd == "delete") {
            string index;
            int value;
            cin >> index >> value;
            delete_entry(index, value);
        } else if (cmd == "find") {
            string index;
            cin >> index;
            find(index);
        }
    }

    return 0;
}
