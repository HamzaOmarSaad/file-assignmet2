// btree_assignment_fixed.cpp
// Fixes "Insertion only works on first node" bug.
// 1. Detaches Node 1 from Free List on first insert.
// 2. Correctly handles Root Splitting and Promotion.
// 3. Implements PDF-style "Max Key" Parent Logic.

#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>

using namespace std;

// ---------------- CONFIGURATION ----------------
static constexpr int INT_BYTES = sizeof(int);

long long nodeSizeBytes(int m) {
    // Flag(4) + m * (Key(4) + Ref(4))
    return 4 + (2 * m * 4);
}

struct Node {
    int flag;           // 0 = leaf, 1 = internal, -1 = free
    vector<int> keys;   // m keys
    vector<int> refs;   // m refs

    Node(int m) {
        flag = -1;
        keys.assign(m, -1);
        refs.assign(m, -1);
    }
};

// ---------------- HELPERS ----------------

// Read 'm' stored in Node 0's key[0] field
int getM(fstream &f) {
    if (!f.is_open()) return 5;
    f.seekg(4, ios::beg);
    int m;
    f.read(reinterpret_cast<char*>(&m), INT_BYTES);
    return m;
}

void writeNodeAt(fstream &f, int nodeIndex, const Node &n, int m) {
    long long off = (long long)nodeIndex * nodeSizeBytes(m);
    f.seekp(off, ios::beg);
    f.write(reinterpret_cast<const char*>(&n.flag), INT_BYTES);
    for (int i = 0; i < m; i++) {
        f.write(reinterpret_cast<const char*>(&n.keys[i]), INT_BYTES);
        f.write(reinterpret_cast<const char*>(&n.refs[i]), INT_BYTES);
    }
}

Node readNodeAt(fstream &f, int nodeIndex, int m) {
    Node n(m);
    long long off = (long long)nodeIndex * nodeSizeBytes(m);
    f.seekg(off, ios::beg);
    if (!f.good()) return n;
    f.read(reinterpret_cast<char*>(&n.flag), INT_BYTES);
    for (int i = 0; i < m; i++) {
        f.read(reinterpret_cast<char*>(&n.keys[i]), INT_BYTES);
        f.read(reinterpret_cast<char*>(&n.refs[i]), INT_BYTES);
    }
    return n;
}

int countKeys(const Node &n) {
    int c = 0;
    for (int k : n.keys) if (k != -1) c++;
    return c;
}

int getMaxKey(const Node &n) {
    int maxK = -1;
    for (int k : n.keys) {
        if (k != -1) {
            if (k > maxK) maxK = k;
        }
    }
    return maxK;
}

void sortNodeContent(Node &n, int m) {
    vector<pair<int, int>> pairs;
    for (int i = 0; i < m; i++) {
        if (n.keys[i] != -1) pairs.push_back({n.keys[i], n.refs[i]});
    }
    sort(pairs.begin(), pairs.end());

    fill(n.keys.begin(), n.keys.end(), -1);
    fill(n.refs.begin(), n.refs.end(), -1);

    for (int i = 0; i < (int)pairs.size(); i++) {
        n.keys[i] = pairs[i].first;
        n.refs[i] = pairs[i].second;
    }
}

void updateParentMax(fstream &f, int parentIdx, int childIdx, int newMax, int m) {
    if (parentIdx == -1) return;
    Node p = readNodeAt(f, parentIdx, m);
    bool changed = false;
    for (int i = 0; i < m; i++) {
        if (p.refs[i] == childIdx) {
            if (p.keys[i] != newMax) {
                p.keys[i] = newMax;
                changed = true;
            }
            break;
        }
    }
    if (changed) {
        sortNodeContent(p, m);
        writeNodeAt(f, parentIdx, p, m);
    }
}

// ---------------- REQUIRED FUNCTIONS ----------------

void CreateIndexFileFile(char* filename, int numberOfRecords, int m) {
    fstream f(filename, ios::out | ios::binary | ios::trunc);
    for (int i = 0; i < numberOfRecords; i++) {
        Node n(m);
        if (i == 0) {
            // Node 0 points to 1 initially (as per PDF table)
            // AND stores 'm' in keys[0]
            n.refs[0] = (numberOfRecords > 1) ? 1 : -1;
            n.keys[0] = m;
        } else {
            n.refs[0] = (i < numberOfRecords - 1) ? i + 1 : -1;
        }
        writeNodeAt(f, i, n, m);
    }
    f.close();
}

int allocateNode(fstream &f, int m) {
    Node head = readNodeAt(f, 0, m);
    int freeIdx = head.refs[0];
    if (freeIdx == -1) return -1;

    Node nextFree = readNodeAt(f, freeIdx, m);
    head.refs[0] = nextFree.refs[0];
    head.keys[0] = m; // Preserve m
    writeNodeAt(f, 0, head, m);

    Node newNode(m);
    newNode.flag = 0;
    writeNodeAt(f, freeIdx, newNode, m);
    return freeIdx;
}
int InsertNewRecordAtIndex(char* filename, int RecordID, int Reference) {
    fstream f(filename, ios::in | ios::out | ios::binary);
    if (!f.is_open()) return -1;
    int m = getM(f);

    Node root = readNodeAt(f, 1, m);

    // --- 1. HANDLE FIRST INSERT (Uninitialized Root) ---
    if (root.flag == -1) {
        Node head = readNodeAt(f, 0, m);
        if (head.refs[0] == 1) {
            // Detach Node 1 from free list
            int nextFree = root.refs[0];
            head.refs[0] = nextFree;
            writeNodeAt(f, 0, head, m);
        }
        root.flag = 0;
        root.keys[0] = RecordID;
        root.refs[0] = Reference;
        for(int i=1; i<m; i++) { root.keys[i] = -1; root.refs[i] = -1; }
        writeNodeAt(f, 1, root, m);
        f.close(); return 1;
    }

    // --- 2. TRAVERSE TO LEAF ---
    vector<int> path;
    int curIdx = 1;
    while (true) {
        path.push_back(curIdx);
        Node cur = readNodeAt(f, curIdx, m);
        if (cur.flag == 0) break;

        int nextIdx = -1;
        for (int i = 0; i < m; i++) {
            if (cur.keys[i] != -1 && cur.keys[i] >= RecordID) {
                nextIdx = cur.refs[i];
                break;
            }
        }
        if (nextIdx == -1) {
            for(int i=m-1; i>=0; i--) if(cur.refs[i]!=-1) { nextIdx = cur.refs[i]; break; }
        }
        if (nextIdx == -1) { f.close(); return -1; }
        curIdx = nextIdx;
    }

    int leafIdx = path.back();
    Node leaf = readNodeAt(f, leafIdx, m);

    // Check duplicates
    for(int k : leaf.keys) if(k == RecordID) { f.close(); return -1; }

    // --- 3. SIMPLE INSERT (No Split) ---
    if (countKeys(leaf) < m) {
        int oldMax = getMaxKey(leaf);
        for(int i=0; i<m; i++) {
            if(leaf.keys[i] == -1) { leaf.keys[i]=RecordID; leaf.refs[i]=Reference; break; }
        }
        sortNodeContent(leaf, m);
        writeNodeAt(f, leafIdx, leaf, m);

        // Update Parent Keys if Max Changed
        int newMax = getMaxKey(leaf);
        if (newMax != oldMax && path.size() > 1) {
             for(int i = (int)path.size()-2; i >= 0; i--) {
                int child = path[i+1];
                int parent = path[i];
                Node p = readNodeAt(f, parent, m);
                bool updated = false;
                for(int k=0; k<m; k++) {
                    if(p.refs[k] == child) {
                        Node c = readNodeAt(f, child, m);
                        int cMax = getMaxKey(c);
                        if(p.keys[k] != cMax) {
                            p.keys[k] = cMax;
                            updated = true;
                        }
                    }
                }
                if(updated) {
                    sortNodeContent(p, m);
                    writeNodeAt(f, parent, p, m);
                } else break;
            }
        }
        f.close(); return leafIdx;
    }

    // --- 4. SPLIT LOGIC ---
    // Prepare all data (m+1 items)
    vector<pair<int, int>> all;
    for (int i = 0; i < m; i++) all.push_back({leaf.keys[i], leaf.refs[i]});
    all.push_back({RecordID, Reference});
    sort(all.begin(), all.end());

    int mid = (m + 1) / 2;

    // *** SPECIAL CASE: ROOT SPLIT (Node 1) ***
    // We handle this explicitly to enforce Node 2 = Left, Node 3 = Right
    if (leafIdx == 1) {
        int leftNodeIdx = allocateNode(f, m);  // Guaranteed Node 2
        int rightNodeIdx = allocateNode(f, m); // Guaranteed Node 3

        Node leftNode(m), rightNode(m);
        leftNode.flag = 0; rightNode.flag = 0; // Both are leaves

        // Distribute Data
        for (int i = 0; i < mid; i++) {
            leftNode.keys[i] = all[i].first; leftNode.refs[i] = all[i].second;
        }
        for (int i = mid; i < (int)all.size(); i++) {
            rightNode.keys[i - mid] = all[i].first; rightNode.refs[i - mid] = all[i].second;
        }

        // Write Children
        writeNodeAt(f, leftNodeIdx, leftNode, m);
        writeNodeAt(f, rightNodeIdx, rightNode, m);

        // Rewrite Root (Node 1) as Parent
        Node newRoot(m);
        newRoot.flag = 1;
        newRoot.keys[0] = getMaxKey(leftNode);
        newRoot.refs[0] = leftNodeIdx;
        newRoot.keys[1] = getMaxKey(rightNode);
        newRoot.refs[1] = rightNodeIdx;

        writeNodeAt(f, 1, newRoot, m);

        // Return the actual location of the record
        bool inRight = false;
        for(int k : rightNode.keys) if(k == RecordID) inRight = true;

        f.close();
        return inRight ? rightNodeIdx : leftNodeIdx;
    }

    // *** NORMAL SPLIT (Not Root) ***
    int rightIdx = allocateNode(f, m);
    Node rightNode(m);
    rightNode.flag = leaf.flag;

    fill(leaf.keys.begin(), leaf.keys.end(), -1);
    fill(leaf.refs.begin(), leaf.refs.end(), -1);

    for (int i = 0; i < mid; i++) {
        leaf.keys[i] = all[i].first; leaf.refs[i] = all[i].second;
    }
    for (int i = mid; i < (int)all.size(); i++) {
        rightNode.keys[i - mid] = all[i].first; rightNode.refs[i - mid] = all[i].second;
    }

    writeNodeAt(f, leafIdx, leaf, m);
    writeNodeAt(f, rightIdx, rightNode, m);

    int returnIdx = leafIdx;
    bool inRight = false;
    for(int k : rightNode.keys) if(k == RecordID) inRight = true;
    if(inRight) returnIdx = rightIdx;

    int leftMax = getMaxKey(leaf);
    int rightMax = getMaxKey(rightNode);
    int childIdxLeft = leafIdx;
    int childIdxRight = rightIdx;

    path.pop_back();

    // Propagate Up
    while (true) {
        if (path.empty()) {
            // Root Split (Upper Level)
            // If an INTERNAL node splits and propagates to root
            int newLeftIdx = allocateNode(f, m);
            Node newLeft = readNodeAt(f, childIdxLeft, m);
            writeNodeAt(f, newLeftIdx, newLeft, m);

            if (returnIdx == childIdxLeft) returnIdx = newLeftIdx;

            Node newRoot(m);
            newRoot.flag = 1;
            newRoot.keys[0] = leftMax;
            newRoot.refs[0] = newLeftIdx;
            newRoot.keys[1] = rightMax;
            newRoot.refs[1] = childIdxRight;

            writeNodeAt(f, 1, newRoot, m);
            f.close(); return returnIdx;
        }

        int parentIdx = path.back();
        path.pop_back();
        Node parent = readNodeAt(f, parentIdx, m);

        vector<pair<int,int>> pItems;
        for(int i=0; i<m; i++) {
            if(parent.keys[i] != -1) {
                if(parent.refs[i] == childIdxLeft) {
                    pItems.push_back({leftMax, childIdxLeft});
                } else {
                    pItems.push_back({parent.keys[i], parent.refs[i]});
                }
            }
        }
        pItems.push_back({rightMax, childIdxRight});
        sort(pItems.begin(), pItems.end());

        if ((int)pItems.size() <= m) {
            fill(parent.keys.begin(), parent.keys.end(), -1);
            fill(parent.refs.begin(), parent.refs.end(), -1);
            for(int i=0; i<(int)pItems.size(); i++) {
                parent.keys[i] = pItems[i].first;
                parent.refs[i] = pItems[i].second;
            }
            writeNodeAt(f, parentIdx, parent, m);
            f.close(); return returnIdx;
        }

        int pMid = (m + 1) / 2;
        int pRightIdx = allocateNode(f, m);
        Node pRight(m); pRight.flag = 1;

        fill(parent.keys.begin(), parent.keys.end(), -1);
        fill(parent.refs.begin(), parent.refs.end(), -1);

        for (int i = 0; i < pMid; i++) {
            parent.keys[i] = pItems[i].first; parent.refs[i] = pItems[i].second;
        }
        for (int i = pMid; i < (int)pItems.size(); i++) {
            pRight.keys[i - pMid] = pItems[i].first; pRight.refs[i - pMid] = pItems[i].second;
        }

        writeNodeAt(f, parentIdx, parent, m);
        writeNodeAt(f, pRightIdx, pRight, m);

        leftMax = getMaxKey(parent);
        rightMax = getMaxKey(pRight);
        childIdxLeft = parentIdx;
        childIdxRight = pRightIdx;
    }
}

void DeleteRecordFromIndex(char* filename, int RecordID) {
    fstream f(filename, ios::in | ios::out | ios::binary);
    if (!f.is_open()) return;
    int m = getM(f);

    vector<int> path;
    int curIdx = 1;
    Node cur = readNodeAt(f, curIdx, m);
    if (cur.flag == -1) { f.close(); return; }

    while (cur.flag != 0) {
        path.push_back(curIdx);
        int nextIdx = -1;
        for (int i = 0; i < m; i++) {
            if (cur.keys[i] != -1 && cur.keys[i] >= RecordID) {
                nextIdx = cur.refs[i];
                break;
            }
        }
        if (nextIdx == -1) {
             for(int i=m-1; i>=0; i--) if(cur.refs[i]!=-1) { nextIdx=cur.refs[i]; break; }
        }
        if (nextIdx == -1) { f.close(); return; }
        curIdx = nextIdx;
        cur = readNodeAt(f, curIdx, m);
    }

    bool found = false;
    for (int i = 0; i < m; i++) {
        if (cur.keys[i] == RecordID) {
            cur.keys[i] = -1;
            cur.refs[i] = -1;
            found = true;
            break;
        }
    }
    if (!found) { f.close(); return; }

    sortNodeContent(cur, m);
    writeNodeAt(f, curIdx, cur, m);

    if (!path.empty()) {
        int newMax = getMaxKey(cur);
        updateParentMax(f, path.back(), curIdx, newMax, m);
    }
    f.close();
}

int SearchARecord(char* filename, int RecordID) {
    fstream f(filename, ios::in | ios::binary);
    if (!f.is_open()) return -1;
    int m = getM(f);

    int curIdx = 1;
    Node cur = readNodeAt(f, curIdx, m);
    if (cur.flag == -1) { f.close(); return -1; }

    while (cur.flag != 0) {
        int nextIdx = -1;
        for (int i = 0; i < m; i++) {
            if (cur.keys[i] != -1 && cur.keys[i] >= RecordID) {
                nextIdx = cur.refs[i];
                break;
            }
        }
        if (nextIdx == -1) {
             for(int i=m-1; i>=0; i--) if(cur.refs[i]!=-1) { nextIdx = cur.refs[i]; break; }
        }
        if (nextIdx == -1) { f.close(); return -1; }
        curIdx = nextIdx;
        cur = readNodeAt(f, curIdx, m);
    }

    for (int i = 0; i < m; i++) {
        if (cur.keys[i] == RecordID) {
            int ref = cur.refs[i];
            f.close();
            return ref;
        }
    }
    f.close();
    return -1;
}

void DisplayIndexFileContent(char* filename) {
    fstream f(filename, ios::in | ios::binary);
    if (!f.is_open()) return;
    int m = getM(f);

    f.seekg(0, ios::end);
    long long size = f.tellg();
    int count = size / nodeSizeBytes(m);

    for (int i = 0; i < count; i++) {
        Node n = readNodeAt(f, i, m);
        cout << "Node " << i << ": [" << n.flag << "]";
        for(int j=0; j<m; j++) {
            cout << " (" << n.keys[j] << "," << n.refs[j] << ")";
        }
        cout << "\n";
    }
    f.close();
}

int main() {
    char filename[] = "btree_index_final.idx";

    int choice;
    CreateIndexFileFile(filename, 10, 5);

    while (true) {

        cout << "\n--- B-Tree Manager (M=" << 5 << ") ---\n";
        cout << "1. Insert Record\n";
        cout << "2. Search Record\n";
        cout << "3. Delete Record\n";
        cout << "4. Display File Content\n";
        cout << "5. hard coded test case\n";
        cout << "6. Exit\n";
        cout << "Select: ";
        cin >> choice;

        if (choice == 1) {
            int id, ref;
            cout << "Enter Record ID: "; cin >> id;
            cout << "Enter Reference: "; cin >> ref;
            int res = InsertNewRecordAtIndex(filename, id, ref);
            if (res != -1) cout << "Inserted successfully at Node " << res << endl;
            else cout << "Insertion failed (Duplicate or Disk Full)\n";
        }
        else if (choice == 2) {
            int id;
            cout << "Enter Record ID to Search: "; cin >> id;
            int ref = SearchARecord(filename, id);
            if (ref != -1) cout << "Found! Reference: " << ref << endl;
            else cout << "Record not found.\n";
        }
        else if (choice == 3) {
            int id;
            cout << "Enter Record ID to Delete: "; cin >> id;
            DeleteRecordFromIndex(filename, id);
        }
        else if (choice == 4) {
            DisplayIndexFileContent(filename);
        }
        else if (choice == 5) {
            cout << "--- Inserting (Page 1) ---\n";
            InsertNewRecordAtIndex(filename, 3, 12);
            InsertNewRecordAtIndex(filename, 7, 24);
            InsertNewRecordAtIndex(filename, 10, 48);
            InsertNewRecordAtIndex(filename, 24, 60);
            InsertNewRecordAtIndex(filename, 14, 72);
            DisplayIndexFileContent(filename);

            cout << "\n--- Inserting 19 (Should split Node 1) ---\n";
            InsertNewRecordAtIndex(filename, 19, 84);
            DisplayIndexFileContent(filename);

            cout << "\n--- Inserting rest (Page 3) ---\n";
            InsertNewRecordAtIndex(filename, 30, 96);
            InsertNewRecordAtIndex(filename, 15, 108);
            InsertNewRecordAtIndex(filename, 1, 120);
            InsertNewRecordAtIndex(filename, 5, 132);
            DisplayIndexFileContent(filename);

            cout << "\n--- Inserting 2 (Node 2 Split) ---\n";
            InsertNewRecordAtIndex(filename, 2, 144);
            DisplayIndexFileContent(filename);

            cout << "\n--- Inserting rest (Page 4) ---\n";
            InsertNewRecordAtIndex(filename, 8, 156);
            InsertNewRecordAtIndex(filename, 9, 168);
            InsertNewRecordAtIndex(filename, 6, 180);
            InsertNewRecordAtIndex(filename, 11, 192);
            InsertNewRecordAtIndex(filename, 12, 204);
            InsertNewRecordAtIndex(filename, 17, 216);
            InsertNewRecordAtIndex(filename, 18, 228);
            DisplayIndexFileContent(filename);
        }
        else if (choice == 6) {
            break;
        }
        else {
            cout << "Invalid choice.\n";
        }
    }

    return 0;
}

