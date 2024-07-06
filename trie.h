#ifndef TRIE_H
#define TRIE_H

typedef struct TrieNode{
    char letter;
    int nChildren;
    struct TrieNode **children;
    int isEndOfWord;
}TrieNode;

void freeTrie(TrieNode* node);
TrieNode* createNode(char letter);
TrieNode* findChild(TrieNode* node, char letter);
void insertWord(TrieNode* root, char* word);
int searchTrie(TrieNode* root, char* word);

#endif