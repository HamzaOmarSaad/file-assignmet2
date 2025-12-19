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

long long nodeSize(int m) {
    // Flag(4) + m * (Key(4) + Ref(4))
    return 4 + (2 * m * 4);
}

struct Node {
    int flag;           // 0 = leaf, 1 = internal, -1 = free
    vector<int> key;   // m keys
    vector<int> ref;   // m refs

    Node(int m) {
        flag = -1;
        key.assign(m, -1);
        ref.assign(m, -1);
    }
};

// ---------------- HELPERS ----------------


void sortNodeContent(Node &n, int m) {
    vector<pair<int, int>> pairs;
    for (int i = 0; i < m; i++) {
        if (n.key[i] != -1) pairs.push_back({n.key[i], n.ref[i]});
    }
    sort(pairs.begin(), pairs.end());

    fill(n.key.begin(), n.key.end(), -1);
    fill(n.ref.begin(), n.ref.end(), -1);

    for (int i = 0; i < (int)pairs.size(); i++) {
        n.key[i] = pairs[i].first;
        n.ref[i] = pairs[i].second;
    }
}
// Read 'm' stored in Node 0's key[0] field
int getM(fstream &f) {
    return 5;
    //if we want to change number of nodes we change this return value
}


int countKeys(const Node &n) {
    int c = 0;
    for (int k : n.key) if (k != -1) c++;
    return c;
}

int getMaxKey(const Node &n) {
    int max = -1;
    for (int k : n.key) {
        if (k != -1) {
            if (k > max) max = k;
        }
    }
    return max;
}


Node readNode(fstream &f, int nodeIndex, int m) {
    Node n(m);
    long long off = (long long)nodeIndex * nodeSize(m);
    f.seekg(off, ios::beg);
    if (!f.good()) return n;
    f.read(reinterpret_cast<char*>(&n.flag), INT_BYTES);
    for (int i = 0; i < m; i++) {
        f.read(reinterpret_cast<char*>(&n.key[i]), INT_BYTES);
        f.read(reinterpret_cast<char*>(&n.ref[i]), INT_BYTES);
    }
    return n;
}




void writeAtNode(fstream &f, int nodeIndex, const Node &n, int m) {
    long long off = (long long)nodeIndex * nodeSize(m);
    f.seekp(off, ios::beg);
    f.write(reinterpret_cast<const char*>(&n.flag), INT_BYTES);
    for (int i = 0; i < m; i++) {
        f.write(reinterpret_cast<const char*>(&n.key[i]), INT_BYTES);
        f.write(reinterpret_cast<const char*>(&n.ref[i]), INT_BYTES);
    }
    f.flush();
}
void updateParentMax(fstream &f, int parentIndx, int childIndx, int newMax, int m) {
    if (parentIndx == -1) return;
    Node p = readNode(f, parentIndx, m);
    bool changed = false;
    for (int i = 0; i < m; i++) {
        if (p.ref[i] == childIndx) {
            if (p.key[i] != newMax) {
                p.key[i] = newMax;
                changed = true;
            }
            break;
        }
    }
    if (changed) {
        sortNodeContent(p, m);
        writeAtNode(f, parentIndx, p, m);
    }
}




// ---------------- REQUIRED FUNCTIONS ----------------

void freeNode(fstream &f, int idx, int m) {
    // 1. Read the Head of the Free List (Node 0)
    Node head = readNode(f, 0, m);

    // 2. Create a "clean" node to overwrite the data at idx
    Node freedNode(m);
    freedNode.flag = -1; // Mark as free/empty

    // 3. Link this node into the free list
    // The new free node points to whatever Node 0 currently points to
    freedNode.ref[0] = head.ref[0];

    // 4. Update Node 0 to point to this newly freed node
    head.ref[0] = idx;

    // 5. Write changes to disk
    writeAtNode(f, idx, freedNode, m);
    writeAtNode(f, 0, head, m);
}
int allocateNode(fstream &f, int m) {
    Node head = readNode(f, 0, m);
    int freeIdx = head.ref[0];
    if (freeIdx == -1) return -1;

    Node nextFree = readNode(f, freeIdx, m);
    head.ref[0] = nextFree.ref[0];
    writeAtNode(f, 0, head, m);

    Node newNode(m);
    newNode.flag = 0;
    writeAtNode(f, freeIdx, newNode, m);
    return freeIdx;
}

void solveUnderflow(fstream &f, int currentIdx, vector<int>& path, int m) {
    Node curr = readNode(f, currentIdx, m);
    int minKeys = m / 2; // e.g., 5/2 = 2

    // If we reached the root (Node 1)
    if (currentIdx == 1) {
        // If root is internal and has only 1 child, that child becomes the new root content
        if (curr.flag == 1 && countKeys(curr) == 1) {
            int childIdx = curr.ref[0];
            Node child = readNode(f, childIdx, m);

            // Move child content to Node 1
            writeAtNode(f, 1, child, m);

            // Free the old child node
            freeNode(f, childIdx, m);
        }
        // If root is leaf, it can have 0 keys (empty file), no underflow fix needed
        return;
    }
    // If node has enough keys, stop
    if (countKeys(curr) >= minKeys) return;
    // 2. GET PARENT & SIBLINGS
    int parentIdx = path.back();
    Node parent = readNode(f, parentIdx, m);
    // Find our position in parent
    int ptrIndex = -1;
    for (int i = 0; i < m; i++) {
        if (parent.ref[i] == currentIdx) { ptrIndex = i; break; }
    }

    int leftSiblingIdx = -1;
    int rightSiblingIdx = -1;
    if (ptrIndex > 0) leftSiblingIdx = parent.ref[ptrIndex - 1];
    // Find next valid ref for right sibling
    if (ptrIndex < m - 1) rightSiblingIdx = parent.ref[ptrIndex + 1];

    // 3. TRY BORROW FROM LEFT
    if (leftSiblingIdx != -1) {
        Node left = readNode(f, leftSiblingIdx, m);
        if (countKeys(left) > minKeys) {
            // Take largest from left
            int maxK = -1, maxR = -1;
            // Find max (last valid item)
            int lastPos = -1;
            for(int k=m-1; k>=0; k--) if(left.key[k] != -1) {
                maxK=left.key[k]; maxR=left.ref[k]; lastPos=k; break;
            }

            // Remove from Left
            left.key[lastPos] = -1; left.ref[lastPos] = -1;
            // Add to Curr (and sort)
            // Find empty slot
            for(int k=0; k<m; k++) if(curr.key[k]==-1) { curr.key[k]=maxK; curr.ref[k]=maxR; break; }
            sortNodeContent(curr, m);

            // Update Disk
            writeAtNode(f, leftSiblingIdx, left, m);
            writeAtNode(f, currentIdx, curr, m);

            // Update Parent Keys (Left Max changed, Curr Max might change)
            updateParentMax(f, parentIdx, leftSiblingIdx, getMaxKey(left), m);
            updateParentMax(f, parentIdx, currentIdx, getMaxKey(curr), m);
            return;
        }
    }

    // 4. TRY BORROW FROM RIGHT
    if (rightSiblingIdx != -1) {
        Node right = readNode(f, rightSiblingIdx, m);
        if (countKeys(right) > minKeys) {
            // Take smallest from right
            int minK = right.key[0];
            int minR = right.ref[0];

            // Remove from Right (Shift remaining)
            right.key[0] = -1; right.ref[0] = -1;
            sortNodeContent(right, m); // Shifts everything left

            // Add to Curr
            for(int k=0; k<m; k++) if(curr.key[k]==-1) { curr.key[k]=minK; curr.ref[k]=minR; break; }
            sortNodeContent(curr, m);

            // Update Disk
            writeAtNode(f, rightSiblingIdx, right, m);
            writeAtNode(f, currentIdx, curr, m);

            // Update Parent Keys
            updateParentMax(f, parentIdx, rightSiblingIdx, getMaxKey(right), m);
            updateParentMax(f, parentIdx, currentIdx, getMaxKey(curr), m);
            return;
        }
    }

    // 5. MERGE WITH LEFT (if borrow failed)
    if (leftSiblingIdx != -1) {
        Node left = readNode(f, leftSiblingIdx, m);

        // Move all items from Curr to Left
        for(int i=0; i<m; i++) {
            if(curr.key[i] != -1) {
                // Find space in Left
                for(int j=0; j<m; j++) {
                    if(left.key[j] == -1) {
                        left.key[j] = curr.key[i];
                        left.ref[j] = curr.ref[i];
                        break;
                    }
                }
            }
        }
        sortNodeContent(left, m);
        writeAtNode(f, leftSiblingIdx, left, m);

        // Free Curr
        freeNode(f, currentIdx, m);

        // Remove Curr from Parent
        parent.key[ptrIndex] = -1;
        parent.ref[ptrIndex] = -1;
        sortNodeContent(parent, m); // Shifts to fill gap
        writeAtNode(f, parentIdx, parent, m);

        // Update Parent Key for Left (it grew)
        updateParentMax(f, parentIdx, leftSiblingIdx, getMaxKey(left), m);

        // RECURSE: Parent might now have too few keys
        path.pop_back(); // Remove parent from path (we are about to pass path to recursive call)
        solveUnderflow(f, parentIdx, path, m);
        return;
    }

    // 6. MERGE WITH RIGHT
    if (rightSiblingIdx != -1) {
        Node right = readNode(f, rightSiblingIdx, m);

        // Move all items from Right to Curr
        for(int i=0; i<m; i++) {
            if(right.key[i] != -1) {
                for(int j=0; j<m; j++) {
                    if(curr.key[j] == -1) {
                        curr.key[j] = right.key[i];
                        curr.ref[j] = right.ref[i];
                        break;
                    }
                }
            }
        }
        sortNodeContent(curr, m);
        writeAtNode(f, currentIdx, curr, m);

        // Free Right
        freeNode(f, rightSiblingIdx, m);

        // Remove Right from Parent
        // Right was at ptrIndex + 1
        // But wait, we need to find exactly where it is now (sorting might have shifted if previous deletes happened? No, we just read it)
        int rightPtrPos = -1;
        for(int i=0; i<m; i++) if(parent.ref[i] == rightSiblingIdx) rightPtrPos = i;

        if(rightPtrPos != -1) {
            parent.key[rightPtrPos] = -1;
            parent.ref[rightPtrPos] = -1;
            sortNodeContent(parent, m);
            writeAtNode(f, parentIdx, parent, m);
        }

        // Update Parent Key for Curr (it grew)
        updateParentMax(f, parentIdx, currentIdx, getMaxKey(curr), m);

        path.pop_back();
        solveUnderflow(f, parentIdx, path, m);
        return;
    }
}


void CreateIndexFileFile(char* filename, int numOfRecords, int m) {
    fstream f(filename, ios::out | ios::binary | ios::trunc);
    for (int i = 0; i < numOfRecords; i++) {
        Node n(m);
        if (i == 0) {
            n.ref[0] = (numOfRecords > 1) ? 1 : -1;
        } else {
            n.ref[0] = (i < numOfRecords - 1) ? i + 1 : -1;
        }
        writeAtNode(f, i, n, m);
    }
    f.close();
}
//--------------------- OPRATIONS ----------------------

void DisplayIndexFileContent(char* filename) {
    fstream f(filename, ios::in | ios::binary);
    if (!f.is_open()) return;
    int m = getM(f);

    f.seekg(0, ios::end);
    long long size = f.tellg();
    int count = size / nodeSize(m);

    for (int i = 0; i < count; i++) {
        Node n = readNode(f, i, m);
        cout << "N " << i << ": {" << n.flag << "}";
        for(int j=0; j<m; j++) {
            cout << " [" << n.key[j] << "," << n.ref[j] << "]";
        }
        cout << "\n";
        cout << "-------------------------------------------------------";
        cout << "\n";
    }
    f.close();
}


int SearchARecord(char* filename, int RecordID) {
    fstream f(filename, ios::in | ios::binary);
    if (!f.is_open()) return -1;
    int m = getM(f);

    int curIdx = 1;
    Node cur = readNode(f, curIdx, m);
    if (cur.flag == -1) { f.close(); return -1; }

    while (cur.flag != 0) {
        int nextIdx = -1;
        for (int i = 0; i < m; i++) {
            if (cur.key[i] != -1 && cur.key[i] >= RecordID) {
                nextIdx = cur.ref[i];
                break;
            }
        }
        if (nextIdx == -1) {
            for(int i=m-1; i>=0; i--) if(cur.ref[i]!=-1) { nextIdx = cur.ref[i]; break; }
        }
        if (nextIdx == -1) { f.close(); return -1; }
        curIdx = nextIdx;
        cur = readNode(f, curIdx, m);
    }

    for (int i = 0; i < m; i++) {
        if (cur.key[i] == RecordID) {
            int ref = cur.ref[i];
            f.close();
            return ref;
        }
    }
    f.close();
    return -1;
}


void DeleteRecordFromIndex(char* filename, int RecordID) {
    fstream f(filename, ios::in | ios::out | ios::binary);
    if (!f.is_open()) return;
    int m = getM(f);

    vector<int> path;
    int curIdx = 1;
    Node cur = readNode(f, curIdx, m);
    if (cur.flag == -1) { f.close(); return; }

    // 1. SEARCH
    while (cur.flag != 0) {
        path.push_back(curIdx);
        int nextIdx = -1;
        for (int i = 0; i < m; i++) {
            if (cur.key[i] != -1 && cur.key[i] >= RecordID) {
                nextIdx = cur.ref[i];
                break;
            }
        }
        if (nextIdx == -1) { for(int i=m-1; i>=0; i--) if(cur.ref[i]!=-1) { nextIdx=cur.ref[i]; break; } }
        if (nextIdx == -1) { f.close(); return; }
        curIdx = nextIdx;
        cur = readNode(f, curIdx, m);
    }

    // 2. DELETE FROM LEAF
    bool found = false;
    for (int i = 0; i < m; i++) {
        if (cur.key[i] == RecordID) {
            cur.key[i] = -1; cur.ref[i] = -1;
            found = true; break;
        }
    }
    if (!found) { cout << "Record " << RecordID << " not found.\n"; f.close(); return; }

    sortNodeContent(cur, m);
    writeAtNode(f, curIdx, cur, m); // This now FLUSHES, so the next read sees the change

    // 3. PROPAGATE UPDATE UPWARDS
    if (!path.empty()) {
        for(int i = (int)path.size()-1; i >= 0; i--) {
            int child = (i == (int)path.size()-1) ? curIdx : path[i+1];
            int parent = path[i];

            Node p = readNode(f, parent, m);
            Node c = readNode(f, child, m); // Now reads the UPDATED child correctly
            int cMax = getMaxKey(c); // Calculates new max (e.g., 9 instead of 10)

            bool updated = false;
            for(int k=0; k<m; k++) {
                if(p.ref[k] == child) {
                    if(p.key[k] != cMax) {
                        p.key[k] = cMax;
                        updated = true;
                    }
                }
            }
            if(updated) {
                sortNodeContent(p, m);
                writeAtNode(f, parent, p, m); // Flushes this parent too
            } else {
                break;
            }
        }
    }

    // 4. CHECK UNDERFLOW
    int minKeys = m / 2;
    if (countKeys(cur) < minKeys) {
        solveUnderflow(f, curIdx, path, m);
    }

    f.close();
    cout << "Record " << RecordID << " deleted successfully.\n";
}

int InsertNewRecordAtIndex(char* filename, int RecID, int Ref) {
    fstream f(filename, ios::in | ios::out | ios::binary);
    if (!f.is_open()) return -1;
    int m = getM(f);

    Node root = readNode(f, 1, m);

    // --- 1. HANDLE FIRST INSERT (Uninitialized Root) ---
    if (root.flag == -1) {
        Node head = readNode(f, 0, m);
        if (head.ref[0] == 1) {
            // Detach Node 1 from free list
            int nextFree = root.ref[0];
            head.ref[0] = nextFree;
            writeAtNode(f, 0, head, m);
        }
        root.flag = 0;
        root.key[0] = RecID;
        root.ref[0] = Ref;
        for(int i=1; i<m; i++) { root.key[i] = -1; root.ref[i] = -1; }
        writeAtNode(f, 1, root, m);
        f.close(); return 1;
    }

    // --- 2. TRAVERSE TO LEAF ---
    vector<int> path;
    int curIdx = 1;
    while (true) {
        path.push_back(curIdx);
        Node cur = readNode(f, curIdx, m);
        if (cur.flag == 0) break;

        int nextIdx = -1;
        for (int i = 0; i < m; i++) {
            if (cur.key[i] != -1 && cur.key[i] >= RecID) {
                nextIdx = cur.ref[i];
                break;
            }
        }
        if (nextIdx == -1) {
            for(int i=m-1; i>=0; i--) if(cur.ref[i]!=-1) { nextIdx = cur.ref[i]; break; }
        }
        if (nextIdx == -1) { f.close(); return -1; }
        curIdx = nextIdx;
    }

    int leafIdx = path.back();
    Node leaf = readNode(f, leafIdx, m);

    // Check duplicates
    for(int k : leaf.key) if(k == RecID) { f.close(); return -1; }

    // --- 3. SIMPLE INSERT (No Split) ---
    if (countKeys(leaf) < m) {
        int oldMax = getMaxKey(leaf);
        for(int i=0; i<m; i++) {
            if(leaf.key[i] == -1) { leaf.key[i]=RecID; leaf.ref[i]=Ref; break; }
        }
        sortNodeContent(leaf, m);
        writeAtNode(f, leafIdx, leaf, m);

        // Update Parent Keys if Max Changed
        int newMax = getMaxKey(leaf);
        if (newMax != oldMax && path.size() > 1) {
             for(int i = (int)path.size()-2; i >= 0; i--) {
                int child = path[i+1];
                int parent = path[i];
                Node p = readNode(f, parent, m);
                bool updated = false;
                for(int k=0; k<m; k++) {
                    if(p.ref[k] == child) {
                        Node c = readNode(f, child, m);
                        int cMax = getMaxKey(c);
                        if(p.key[k] != cMax) {
                            p.key[k] = cMax;
                            updated = true;
                        }
                    }
                }
                if(updated) {
                    sortNodeContent(p, m);
                    writeAtNode(f, parent, p, m);
                } else break;
            }
        }
        f.close(); return leafIdx;
    }

    // --- 4. SPLIT LOGIC ---
    // Prepare all data (m+1 items)
    vector<pair<int, int>> all;
    for (int i = 0; i < m; i++) all.push_back({leaf.key[i], leaf.ref[i]});
    all.push_back({RecID, Ref});
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
            leftNode.key[i] = all[i].first; leftNode.ref[i] = all[i].second;
        }
        for (int i = mid; i < (int)all.size(); i++) {
            rightNode.key[i - mid] = all[i].first; rightNode.ref[i - mid] = all[i].second;
        }

        // Write Children
        writeAtNode(f, leftNodeIdx, leftNode, m);
        writeAtNode(f, rightNodeIdx, rightNode, m);

        // Rewrite Root (Node 1) as Parent
        Node newRoot(m);
        newRoot.flag = 1;
        newRoot.key[0] = getMaxKey(leftNode);
        newRoot.ref[0] = leftNodeIdx;
        newRoot.key[1] = getMaxKey(rightNode);
        newRoot.ref[1] = rightNodeIdx;

        writeAtNode(f, 1, newRoot, m);

        // Return the actual location of the record
        bool inRight = false;
        for(int k : rightNode.key) if(k == RecID) inRight = true;

        f.close();
        return inRight ? rightNodeIdx : leftNodeIdx;
    }

    // *** NORMAL SPLIT (Not Root) ***
    int rightIdx = allocateNode(f, m);
    Node rightNode(m);
    rightNode.flag = leaf.flag;

    fill(leaf.key.begin(), leaf.key.end(), -1);
    fill(leaf.ref.begin(), leaf.ref.end(), -1);

    for (int i = 0; i < mid; i++) {
        leaf.key[i] = all[i].first; leaf.ref[i] = all[i].second;
    }
    for (int i = mid; i < (int)all.size(); i++) {
        rightNode.key[i - mid] = all[i].first; rightNode.ref[i - mid] = all[i].second;
    }

    writeAtNode(f, leafIdx, leaf, m);
    writeAtNode(f, rightIdx, rightNode, m);

    int returnIdx = leafIdx;
    bool inRight = false;
    for(int k : rightNode.key) if(k == RecID) inRight = true;
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
            Node newLeft = readNode(f, childIdxLeft, m);
            writeAtNode(f, newLeftIdx, newLeft, m);

            if (returnIdx == childIdxLeft) returnIdx = newLeftIdx;

            Node newRoot(m);
            newRoot.flag = 1;
            newRoot.key[0] = leftMax;
            newRoot.ref[0] = newLeftIdx;
            newRoot.key[1] = rightMax;
            newRoot.ref[1] = childIdxRight;

            writeAtNode(f, 1, newRoot, m);
            f.close(); return returnIdx;
        }

        int parentIdx = path.back();
        path.pop_back();
        Node parent = readNode(f, parentIdx, m);

        vector<pair<int,int>> pItems;
        for(int i=0; i<m; i++) {
            if(parent.key[i] != -1) {
                if(parent.ref[i] == childIdxLeft) {
                    pItems.push_back({leftMax, childIdxLeft});
                } else {
                    pItems.push_back({parent.key[i], parent.ref[i]});
                }
            }
        }
        pItems.push_back({rightMax, childIdxRight});
        sort(pItems.begin(), pItems.end());

        if ((int)pItems.size() <= m) {
            fill(parent.key.begin(), parent.key.end(), -1);
            fill(parent.ref.begin(), parent.ref.end(), -1);
            for(int i=0; i<(int)pItems.size(); i++) {
                parent.key[i] = pItems[i].first;
                parent.ref[i] = pItems[i].second;
            }
            writeAtNode(f, parentIdx, parent, m);
            f.close(); return returnIdx;
        }

        int pMid = (m + 1) / 2;
        int pRightIdx = allocateNode(f, m);
        Node pRight(m); pRight.flag = 1;

        fill(parent.key.begin(), parent.key.end(), -1);
        fill(parent.ref.begin(), parent.ref.end(), -1);

        for (int i = 0; i < pMid; i++) {
            parent.key[i] = pItems[i].first; parent.ref[i] = pItems[i].second;
        }
        for (int i = pMid; i < (int)pItems.size(); i++) {
            pRight.key[i - pMid] = pItems[i].first; pRight.ref[i - pMid] = pItems[i].second;
        }

        writeAtNode(f, parentIdx, parent, m);
        writeAtNode(f, pRightIdx, pRight, m);

        leftMax = getMaxKey(parent);
        rightMax = getMaxKey(pRight);
        childIdxLeft = parentIdx;
        childIdxRight = pRightIdx;
    }
}


//----------------------MAIN---------------------------

int main() {
    char filename[] = "btree";

    int choice;
    CreateIndexFileFile(filename, 10, 5);

    while (true) {

        cout << "\n B-Tree file (M=" << 5 << ") \n";
        cout << "1. Insert Record:\n";
        cout << "\n";
        cout << "2. Search Record:\n";
        cout << "\n";
        cout << "3. Delete Record:\n";
        cout << "\n";
        cout << "4. Display File:\n";
        cout << "\n";
        cout << "5. whole file test case:\n";
        cout << "\n";
        cout << "6. Exit:\n";
        cout << "\n";
        cout << "please enter Your choice: ";
        cout << "\n";
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

            cout << "\n--- Inserting rest (Page 5) ---\n";
            InsertNewRecordAtIndex(filename, 32, 240);
            DisplayIndexFileContent(filename);




            cout << "\n--- delete 10 ---\n";
            DeleteRecordFromIndex(filename,10);
            DisplayIndexFileContent(filename);


            cout << "\n--- delete 9 ---\n";
            DeleteRecordFromIndex(filename,9);
            DisplayIndexFileContent(filename);

            cout << "\n--- delete 8 ---\n";
            DeleteRecordFromIndex(filename,8);
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