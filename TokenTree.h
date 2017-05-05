#ifndef TOKEN_TREE_H_
#define TOKEN_TREE_H_

/* This is based on AATree of the C++ data structure book */

#include <forward_list>
#include <iostream> // For NULL
#include <string>
#include <map>

// TokenTree class
//
// CONSTRUCTION: with ITEM_NOT_FOUND object used to signal failed finds
//
// ******************PUBLIC OPERATIONS*********************
// void insert( x )       --> Insert x
// uint64_t find( x )   --> Return item that matches x
// boolean isEmpty( )     --> Return true if empty; else false
// void makeEmpty( )      --> Remove all items
// void printTree( )      --> Print tree in sorted order

// Node and forward declaration because g++ does
// not understand nested classes.
class TokenTree;

struct AANode {
    uint64_t element;
    TokenTree* next_token;
    AANode* left;
    AANode* right;
    uint16_t level;

    AANode()
        : next_token(NULL)
        , left(NULL)
        , right(NULL)
        , level(1)
    {
    }
    AANode(const uint64_t& e, TokenTree* nt, AANode* lt, AANode* rt, int lv = 1)
        : element(e)
        , next_token(nt)
        , left(lt)
        , right(rt)
        , level(lv)
    {
    }

    friend class TokenTree;
};

typedef std::forward_list<std::pair<unsigned char, TokenTree*> > SkipList;

struct SerializeInfo {
  std::map<const AANode*, int> nodes;
  int32_t node_count;
  std::map<const TokenTree*, int> trees;
  int32_t tree_count;
  std::map<uint64_t, int> elements;
  int32_t element_count;

  SerializeInfo() {
    tree_count = node_count = element_count = 0;
  }
};

class TokenTree {
public:
    TokenTree();
    ~TokenTree();

    TokenTree* find(uint64_t x) const;
    bool isEmpty() const;
    void printTree() const;
    void mark_elements(SerializeInfo &si) const;
 
    void makeEmpty();
    void insert(uint64_t x, TokenTree* next_token);

    const TokenTree& operator=(const TokenTree& rhs);

    int pid;
    SkipList skips;
    AANode* root;
  
private:
    AANode* nullNode;

    // Recursive routines
    void insert(uint64_t x, TokenTree* next_token, AANode*& t);
    void makeEmpty(AANode*& t);
    void printTree(AANode* t, const std::string&) const;
    void mark_elements(AANode *t, SerializeInfo &si) const;
  
    // Rotations
    void skew(AANode*& t) const;
    void split(AANode*& t) const;

    // forbidden
    TokenTree(const TokenTree& rhs);
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
    pid = 0;
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
void TokenTree::insert(uint64_t x, TokenTree* next_token)
{
    insert(x, next_token, root);
}

/**
 * Find item x in the tree.
 * Return the next token tree or NULL
 */
TokenTree* TokenTree::find(uint64_t x) const
{
    AANode* current = root;
    nullNode->element = x;

    for (;;) {
        if (x < current->element)
            current = current->left;
        else if (current->element < x)
            current = current->right;
        else if (current != nullNode)
            return current->next_token;
        else
            return NULL;
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
        printTree(root, "");
}

/**
 * Internal method to insert into a subtree.
 * x is the item to insert.
 * t is the node that roots the tree.
 * Set the new root.
 */
void TokenTree::insert(uint64_t x, TokenTree* next_token, AANode*& t)
{
    if (t == nullNode)
        t = new AANode(x, next_token, nullNode, nullNode);
    else if (x < t->element)
        insert(x, next_token, t->left);
    else if (t->element < x)
        insert(x, next_token, t->right);
    else {
        std::cerr << "Duplicate " << x << " ignored on insert\n";
        return; // Duplicate; do nothing
    }
    skew(t);
    split(t);
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
void TokenTree::printTree(AANode* t, const std::string& indent) const
{
  if (t == nullNode)
    return;
  std::string ni = indent + "  ";
  printTree(t->left, ni);
  std::cout << indent << t->element << " TREE BEGIN" << std::endl;
  t->next_token->printTree();
  std::cout << indent << "TREE END\n";
  printTree(t->right, ni);
}

void TokenTree::mark_elements(SerializeInfo &si) const
{
  for (SkipList::const_iterator it = skips.begin(); it != skips.end(); ++it)
      it->second->mark_elements(si);
 
  if (si.trees.find(this) == si.trees.end())
      si.trees[this] = si.tree_count++;
    
  mark_elements(root, si);
}

void TokenTree::mark_elements(AANode *t, SerializeInfo &si) const
{
  if (t == nullNode)
    return;
  mark_elements(t->left, si);
  mark_elements(t->right, si);
  if (si.elements.find(t->element) == si.elements.end())
    si.elements[t->element] = si.element_count++;
  // unlikely
  if (si.nodes.find(t) == si.nodes.end())
    si.nodes[t] = si.node_count++;
  t->next_token->mark_elements(si);
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

#endif
