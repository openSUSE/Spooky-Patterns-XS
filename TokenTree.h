#ifndef TOKEN_TREE_H_
#define TOKEN_TREE_H_

/* This is based on AATree of the C++ data structure book */

#include <iostream> // For NULL

// TokenTree class
//
// CONSTRUCTION: with ITEM_NOT_FOUND object used to signal failed finds
//
// ******************PUBLIC OPERATIONS*********************
// void insert( x )       --> Insert x
// void remove( x )       --> Remove x
// uint64_t find( x )   --> Return item that matches x
// boolean isEmpty( )     --> Return true if empty; else false
// void makeEmpty( )      --> Remove all items
// void printTree( )      --> Print tree in sorted order

// Node and forward declaration because g++ does
// not understand nested classes.
class TokenTree;

class AANode {
    uint64_t element;
    AANode* left;
    AANode* right;
    int level;

    AANode()
        : left(NULL)
        , right(NULL)
        , level(1)
    {
    }
    AANode(const uint64_t& e, AANode* lt, AANode* rt, int lv = 1)
        : element(e)
        , left(lt)
        , right(rt)
        , level(lv)
    {
    }

    friend class TokenTree;
};

class TokenTree {
public:
    TokenTree();
    TokenTree(const TokenTree& rhs);
    ~TokenTree();

    uint64_t find(uint64_t x) const;
    bool isEmpty() const;
    void printTree() const;

    void makeEmpty();
    void insert(uint64_t x);
    void remove(uint64_t x);

    const TokenTree& operator=(const TokenTree& rhs);

private:
    AANode* root;
    AANode* nullNode;

    // Recursive routines
    void insert(uint64_t x, AANode*& t);
    void remove(uint64_t x, AANode*& t);
    void makeEmpty(AANode*& t);
    void printTree(AANode* t) const;

    // Rotations
    void skew(AANode*& t) const;
    void split(AANode*& t) const;
    AANode* clone(AANode* t) const;
};

/**
         * Construct the tree.
         */
TokenTree::TokenTree()
{
    nullNode = new AANode;
    nullNode->left = nullNode->right = nullNode;
    nullNode->level = 0;
    root = nullNode;
}

/*
 * Copy constructor.
 */
TokenTree::TokenTree(const TokenTree& rhs)
{
    nullNode = new AANode;
    nullNode->left = nullNode->right = nullNode;
    nullNode->level = 0;
    root = clone(rhs.root);
}

/* 
 * Destructor for the tree.
 */
TokenTree::~TokenTree()
{
    makeEmpty();
    delete nullNode;
}

/*
 * Insert x into the tree; duplicates are ignored.
 */
void TokenTree::insert(uint64_t x)
{
    insert(x, root);
}

/*
 * Remove x from the tree. Nothing is done if x is not found.
 */
void TokenTree::remove(uint64_t x)
{
    remove(x, root);
}

/**
 * Find item x in the tree.
 * Return the matching item or ITEM_NOT_FOUND if not found.
 */
uint64_t TokenTree::find(uint64_t x) const
{
    AANode* current = root;
    nullNode->element = x;

    for (;;) {
        if (x < current->element)
            current = current->left;
        else if (current->element < x)
            current = current->right;
        else if (current != nullNode)
            return current->element;
        else
            return 0;
    }
}

/**
 * Make the tree logically empty.
 */
void TokenTree::makeEmpty()
{
    makeEmpty(root);
}

/**
 * Test if the tree is logically empty.
 * Return true if empty, false otherwise.
 */
bool TokenTree::isEmpty() const
{
    return root == nullNode;
}

/**
 * Print the tree contents in sorted order.
 */
void TokenTree::printTree() const
{
    if (root == nullNode)
        std::cout << "Empty tree" << std::endl;
    else
        printTree(root);
}

/**
         * Deep copy.
         */
const TokenTree&
TokenTree::operator=(const TokenTree& rhs)
{
    if (this != &rhs) {
        makeEmpty();
        root = clone(rhs.root);
    }

    return *this;
}

/**
         * Internal method to insert into a subtree.
         * x is the item to insert.
         * t is the node that roots the tree.
         * Set the new root.
         */
void TokenTree::
    insert(uint64_t x, AANode*& t)
{
    if (t == nullNode)
        t = new AANode(x, nullNode, nullNode);
    else if (x < t->element)
        insert(x, t->left);
    else if (t->element < x)
        insert(x, t->right);
    else
        return; // Duplicate; do nothing

    skew(t);
    split(t);
}

/*
 * Internal method to remove from a subtree.
 * x is the item to remove.
 * t is the node that roots the tree.
 * Set the new root.
 */
void TokenTree::remove(uint64_t x, AANode*& t)
{
    static AANode *lastNode, *deletedNode = nullNode;

    if (t != nullNode) {
        // Step 1: Search down the tree and set lastNode and deletedNode
        lastNode = t;
        if (x < t->element)
            remove(x, t->left);
        else {
            deletedNode = t;
            remove(x, t->right);
        }

        // Step 2: If at the bottom of the tree and
        //         x is present, we remove it
        if (t == lastNode) {
            if (deletedNode == nullNode || x != deletedNode->element)
                return; // Item not found; do nothing
            deletedNode->element = t->element;
            deletedNode = nullNode;
            t = t->right;
            delete lastNode;
        }

        // Step 3: Otherwise, we are not at the bottom; rebalance
        else if (t->left->level < t->level - 1 || t->right->level < t->level - 1) {
            if (t->right->level > --t->level)
                t->right->level = t->level;
            skew(t);
            skew(t->right);
            skew(t->right->right);
            split(t);
            split(t->right);
        }
    }
}

/**
         * Internal method to make subtree empty.
         */
void TokenTree::makeEmpty(AANode*& t)
{
    if (t != nullNode) {
        makeEmpty(t->left);
        makeEmpty(t->right);
        delete t;
    }
    t = nullNode;
}

/**
 * Internal method to print a subtree in sorted order.
 * @param t the node that roots the tree.
 */
void TokenTree::printTree(AANode* t) const
{
    if (t != nullNode) {
        printTree(t->left);
        std::cout << t->element << std::endl;
        printTree(t->right);
    }
}

/**
 * Skew primitive for AA-trees.
 * t is the node that roots the tree.
 */
void TokenTree::skew(AANode*& t) const
{
    if (t->left->level == t->level) {
        AANode* s = t->left;
        t->left = s->right;
        s->right = t;
        t = s;
    }
}

/**
 * Split primitive for AA-trees.
 * t is the node that roots the tree.
 */
void TokenTree::split(AANode*& t) const
{
    if (t->right->right->level == t->level) {
        AANode* s = t->right;
        t->right = s->left;
        s->left = t;
        t = s;
        t->level++;
    }
}

/**
 * Internal method to clone subtree.
 */
AANode*
TokenTree::clone(AANode* t) const
{
    if (t == t->left) // Cannot test against nullNode!!!
        return nullNode;
    else
        return new AANode(t->element, clone(t->left),
            clone(t->right), t->level);
}

#endif
