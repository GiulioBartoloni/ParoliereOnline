// la libreria contiene le funzioni per la creazione e mantenimento della Trie
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "macros.h"
#include "trie.h"

//la funzione libera ricorsivamente tutta la memoria occupata dalla trie passata in input
void freeTrie(TrieNode* node){
    for(int i = 0; i < node->nChildren; i++)
        freeTrie(node->children[i]);
    free(node->children);
    free(node);
}

//la funzione permette di creare un nodo con la lettera specificata
TrieNode* createNode(char letter){
    TrieNode* node;

    SYSCN(node, (TrieNode*)malloc(sizeof(TrieNode)), "malloc:");
    node->letter = letter;
    node->nChildren = 0;
    node->children = NULL;
    node->isEndOfWord = 0;
    
    return node;
}

//la funzione cerca se e' presente un figlio con la lettera letter tra quelli del nodo node
//ritorna il figlio se presente, NULL altrimenti
TrieNode* findChild(TrieNode* node, char letter){
    for(int i = 0; i < node->nChildren; i++){
        if(node->children[i]->letter == letter)
            return node->children[i];
    }
    return NULL;
}

//la funzione inserisce una parola all'interno della trie
//finchè trovo la lettera, continuo ad "avanzare" 
//se mi fermo, inizio a creare i nodi necessari
//alla fine imposto isEndOfWord per indicare che il nodo è terminante
void insertWord(TrieNode* root, char* word){
    TrieNode* current = root;
    for(int i = 0; i < strlen(word); i++){
        TrieNode* child = findChild(current,word[i]);
        if(!child){
            child = createNode(word[i]);
            SYSCN(current->children, (TrieNode**)realloc(current->children,sizeof(TrieNode*)*(current->nChildren+1)), "realloc:");
            current->children[current->nChildren] = child;
            current->nChildren++;
        }
        current = child;
    }
    current->isEndOfWord = 1;
}

//la funzione cerca la parola word nella trie indicata dal nodo root
int searchTrie(TrieNode* root, char* word){
    TrieNode* current=root;
    for(int i = 0;i < strlen(word); i++){
        current = findChild(current, word[i]);
        if(!current)
            return 0;
    }
    return current->isEndOfWord;
}