#ifndef TOKEN_TREE_H_
#define TOKEN_TREE_H_

/* This is based on AATree of the C++ data structure book */

#include <forward_list>
#include <iostream> // For NULL
#include <map>
#include <string>

// TokenTree class
//
// CONSTRUCTION: with ITEM_NOT_FOUND object used to signal failed finds
//
// ******************PUBLIC OPERATIONS*********************
// void insert( x )       --> Insert x
// uint64_t find( x )   --> Return item that matches x

// Node and forward declaration because g++ does
// not understand nested classes.
class TokenTree;

class AANode;

struct AANode {

    uint64_t element;
    TokenTree* next_token;
    uint32_t left;
    uint32_t right;
    uint16_t level;

    AANode(const uint64_t& e, TokenTree* nt, int lt, int rt, int lv = 1)
        : element(e)
        , next_token(nt)
        , left(lt)
        , right(rt)
        , level(lv)
    {
    }

    friend class TokenTree;

private:
    AANode()
    {
    }
};

typedef std::forward_list<std::pair<unsigned char, TokenTree*> > SkipList;

struct SerializeInfo {
    std::map<const TokenTree*, int> trees;
    int32_t tree_count;
    std::map<uint64_t, int> elements;
    int32_t element_count;

    SerializeInfo()
    {
        tree_count = element_count = 0;
    }
};

class TokenTree {
public:
    TokenTree();
    ~TokenTree();

    TokenTree* find(uint64_t x) const;
    void mark_elements(SerializeInfo& si) const;

    void insert(uint64_t x, TokenTree* next_token);

    const TokenTree& operator=(const TokenTree& rhs);
    void printTree() const;

    uint32_t pid;
    SkipList* skips;
    uint32_t root;

    static std::vector<AANode> nodes;

    void initNull()
    {
        if (nodes.empty())
            nodes.emplace_back(0, (TokenTree*)0, 0, 0, 0);
        root = 0;
    }

    // Recursive routines
    int insert(uint64_t x, TokenTree* next_token, int t);
    void printTree(int t, const std::string&) const;
    void mark_elements(int t, SerializeInfo& si) const;

    // Rotations
    int skew(int t);
    int split(int t);

    // forbidden
    TokenTree(const TokenTree& rhs);
};

/**
 * Construct the tree.
 */
TokenTree::TokenTree()
{
    initNull();
    pid = 0;
    skips = 0;
}

/* 
 * Destructor for the tree.
 */
TokenTree::~TokenTree()
{
    delete skips;
}

/*
 * Insert x into the tree; duplicates are ignored.
 */
void TokenTree::insert(uint64_t x, TokenTree* next_token)
{
    root = insert(x, next_token, root);
}

void TokenTree::printTree() const
{
    if (root == 0)
        std::cerr << "Empty tree" << std::endl;
    else
        printTree(root, "");
}

void TokenTree::printTree(int t, const std::string& indent) const
{
    if (t == 0)
        return;
    std::string ni = indent + "  ";
    printTree(nodes[t].left, ni);
    fprintf(stderr, "%s(%d-%d-%d) %lu\n", indent.c_str(), nodes[t].left, t, nodes[t].right, nodes[t].element);
    printTree(nodes[t].right, ni);
}

/**
 * Find item x in the tree.
 * Return the next token tree or NULL
 */
TokenTree* TokenTree::find(uint64_t x) const
{
    int current = root;
    nodes[0].element = x;

    for (;;) {
        const AANode& cn = nodes[current];
        if (x < cn.element) {
            current = cn.left;
        } else if (cn.element < x) {
            current = cn.right;
        } else
            return cn.next_token;
    }
}

/**
 * Internal method to insert into a subtree.
 * x is the item to insert.
 * t is the node that roots the tree.
 * Set the new root.
 */
int TokenTree::insert(uint64_t x, TokenTree* next_token, int t)
{
    if (t == 0) {
        nodes.emplace_back(x, next_token, 0, 0);
        t = nodes.size() - 1;
    } else if (x < nodes[t].element) {
        int t2 = insert(x, next_token, nodes[t].left);
        nodes[t].left = t2;
    } else if (nodes[t].element < x) {
        int t2 = insert(x, next_token, nodes[t].right);
        nodes[t].right = t2;
    } else {
        std::cerr << "Duplicate " << x << " ignored on insert\n";
        return t; // Duplicate; do nothing
    }
    t = skew(t);
    t = split(t);
    return t;
}

void TokenTree::mark_elements(SerializeInfo& si) const
{
    if (skips) {
        for (SkipList::const_iterator it = skips->begin(); it != skips->end(); ++it)
            it->second->mark_elements(si);
    }

    if (si.trees.find(this) == si.trees.end())
        si.trees[this] = si.tree_count++;

    mark_elements(root, si);
}

void TokenTree::mark_elements(int t, SerializeInfo& si) const
{
    if (t == 0)
        return;
    mark_elements(nodes[t].left, si);
    mark_elements(nodes[t].right, si);
    if (si.elements.find(nodes[t].element) == si.elements.end())
        si.elements[nodes[t].element] = si.element_count++;
    // unlikely
    nodes[t].next_token->mark_elements(si);
}

/**
 * Skew primitive for AA-trees.
 * t is the node that roots the tree.
 */
int TokenTree::skew(int t)
{
    AANode& tn = nodes[t];
    AANode& ln = nodes[tn.left];
    if (ln.level == tn.level) {
        int s = tn.left;
        tn.left = ln.right;
        ln.right = t;
        return s;
    }
    return t;
}

/**
 * Split primitive for AA-trees.
 * t is the node that roots the tree.
 */
int TokenTree::split(int t)
{
    AANode& tn = nodes[t];
    AANode& rn = nodes[tn.right];
    AANode& rrn = nodes[rn.right];
    if (rrn.level == tn.level) {
        int s = tn.right;
        tn.right = rn.left;
        rn.left = t;
        rn.level++;
        return s;
    }
    return t;
}

#endif
