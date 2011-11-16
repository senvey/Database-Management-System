
#ifndef _ix_h_
#define _ix_h_

#include <vector>
#include <string>
//#include <typeinfo>

#include "../pf/pf.h"
#include "../rm/rm.h"

# define IX_EOF (-1)  // end of the index scan

using namespace std;

typedef enum {
	SUCCESS = 0,
	RECORD_NOT_FOUND,
	KEY_EXISTS,

	INVALID_OPERATION = -1,
	FILE_OP_ERROR = -2,
	FILE_NOT_FOUND = -3,

	ATTRIBUTE_NOT_FOUND = -4,
} ReturnCode;

class IX_IndexHandle;

/******************** Tree Structure ********************/

#define DEFAULT_ORDER 2

typedef enum {
	NON_LEAF_NODE = 0,
	LEAF_NODE = 1,
} NodeType;

template <typename KEY>
struct BTreeNode {
	NodeType type;
	BTreeNode<KEY>* parent;
	BTreeNode<KEY>* left;
	BTreeNode<KEY>* right;
	unsigned pos;	// the position in parent node

	vector<KEY> keys;
	vector<RID> rids;
	vector<BTreeNode<KEY>*> children;

	int pageNum;	// -1 indicates unsaved page
	int leftPageNum;	// -1 means no left page; this is the most left one
	int rightPageNum;	// -1 means no right page; this is the most right one
	vector<int> childrenPageNums;
};

template <typename Class, typename KEY>
class Functor	// TODO: use static function?
{
public:
	Functor(Class *obj, BTreeNode<KEY>* (Class::*func)(const unsigned, const NodeType)) : _obj(obj), _readNode(func) {};
	BTreeNode<KEY>* operator()(const unsigned pageNum, const NodeType nodeType) { return (*_obj.*_readNode)(pageNum, nodeType); };

private:
	Class *_obj;
	BTreeNode<KEY>* (Class::*_readNode)(const unsigned, const NodeType);
};

template <typename KEY>
class BTree {
public:
	BTree(const unsigned order, IX_IndexHandle *ixHandle, BTreeNode<KEY>* (IX_IndexHandle::*func)(const unsigned, const NodeType));	// grow a tree from the beginning
	BTree(const unsigned order, BTreeNode<KEY> *root, const unsigned height, IX_IndexHandle *ixHandle,
			BTreeNode<KEY>* (IX_IndexHandle::*func)(const unsigned, const NodeType));	// initialize a tree with given root node
	~BTree();

	RC SearchEntry(const KEY key, BTreeNode<KEY> **leafNode, unsigned &pos);
	RC InsertEntry(const KEY key, const RID &rid);
	RC DeleteEntry(const KEY key,const RID &rid);
	RC DeleteTree(BTreeNode<KEY> *Node);

	vector<BTreeNode<KEY>*> GetUpdatedNodes() const;
	vector<BTreeNode<KEY>*> GetDeletedNodes() const;
	void ClearPendingNodes();
	BTreeNode<KEY>* GetRoot() const;
	unsigned GetHeight() const;

protected:
	BTree();
	RC SearchNode(BTreeNode<KEY> *node, const KEY key, const unsigned height, BTreeNode<KEY> **leafNode, unsigned &pos);
	RC Insert(const KEY key, const RID &rid, BTreeNode<KEY> *leafNode, const unsigned pos);
	RC Insert(BTreeNode<KEY> *rightNode);

	RC delete_NLeafNode(BTreeNode<KEY>* Node,unsigned nodeLevel, const KEY key, const RID &rid,int& oldchildPos);
	RC delete_LeafNode(BTreeNode<KEY>* Node, const KEY key,const RID &rid, int& oldchildPos);

	void redistribute_NLeafNode(BTreeNode<KEY>* Node,BTreeNode<KEY>* siblingNode);
	void redistribute_LeafNode(BTreeNode<KEY>* Node,BTreeNode<KEY>* siblingNode);

    void merge_LeafNode(BTreeNode<KEY>* leftNode,BTreeNode<KEY>* rightNode);
	void merge_NLeafNode(BTreeNode<KEY>* leftNode,BTreeNode<KEY>* rightNode);

private:
	void InitRootNode(const NodeType nodeType);

private:
	BTreeNode<KEY>* _root;
	unsigned _order;
	unsigned _height;
	Functor<IX_IndexHandle, KEY> _func_ReadNode;

	vector<BTreeNode<KEY>*> _updated_nodes;
	vector<BTreeNode<KEY>*> _deleted_nodes;
};

/******************** Tree Structure ********************/

class IX_Manager {
 public:
  static IX_Manager* Instance();

  RC CreateIndex(const string tableName,       // create new index
		 const string attributeName);
  RC DestroyIndex(const string tableName,      // destroy an index
		  const string attributeName);
  RC OpenIndex(const string tableName,         // open an index
	       const string attributeName,
	       IX_IndexHandle &indexHandle);
  RC CloseIndex(IX_IndexHandle &indexHandle);  // close index
  
 protected:
  IX_Manager   ();                             // Constructor
  ~IX_Manager  ();                             // Destructor
 
 private:
  static IX_Manager *_ix_manager;
  static PF_Manager *_pf_manager;
};


class IX_IndexHandle {
 public:
  IX_IndexHandle  ();                           // Constructor
  ~IX_IndexHandle ();                           // Destructor

  // The following two functions are using the following format for the passed key value.
  //  1) data is a concatenation of values of the attributes
  //  2) For int and real: use 4 bytes to store the value;
  //     For varchar: use 4 bytes to store the length of characters, then store the actual characters.
  RC InsertEntry(void *key, const RID &rid);  // Insert new index entry
  RC DeleteEntry(void *key, const RID &rid);  // Delete index entry

  RC Open(PF_FileHandle *handle, AttrType keyType);
  RC Close();
  PF_FileHandle* GetFileHandle() const;
  AttrType GetKeyType() const;

 protected:
  template <typename KEY>
  RC InitTree(BTree<KEY> **tree);
  template <typename KEY>
  RC InsertEntry(BTree<KEY> **tree, const KEY key, const RID &rid);
  template <typename KEY>
  BTreeNode<KEY>* ReadNode(const unsigned pageNum, const NodeType nodeType);
  template <typename KEY>
  RC WriteNodes(const vector<BTreeNode<KEY>*> &nodes);
  template <typename KEY>
  RC UpdateMetadata(const BTree<KEY> *tree);

 private:
  PF_FileHandle *_pf_handle;
  AttrType _key_type;
  unsigned _free_page_num;

  BTree<int> *_int_index;
  BTree<float> *_float_index;
};



class IX_IndexScan {
 public:
  IX_IndexScan();  								// Constructor
  ~IX_IndexScan(); 								// Destructor

  // for the format of "value", please see IX_IndexHandle::InsertEntry()
  RC OpenScan(const IX_IndexHandle &indexHandle, // Initialize index scan
	      CompOp      compOp,
	      void        *value);           

  RC GetNextEntry(RID &rid);  // Get next matching entry
  RC CloseScan();             // Terminate index scan
 protected:
  template <typename KEY>
  RC get_next_entry(RID &rid);
  template <typename KEY>
  RC OpenScan(const IX_IndexHandle &indexHandle,
  	      CompOp      compOp,
  	      void        *value);

 private:
  BTreeNode<int> *intNode;
  BTreeNode<float> *floatNode;
  char keyValue[PF_PAGE_SIZE];
  int currentIndex;
  CompOp compOp;
  AttrType type;
  IX_IndexHandle indexHandle;
};

// print out the error message for a given return code
void IX_PrintError (RC rc);


#endif
