/****************************************************************************
 * dictionary.c
 *
 * Computer Science 50
 * Problem Set 6
 *
 * Armaghan Behlum
 * Rob Bowden
 *
 * Implements a dictionary's functionality.
 ***************************************************************************/

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dictionary.h"

#define ALPHABET 27

typedef struct node
{
    bool word;
    struct node* children[ALPHABET];
}
node;

// set up recursive function for unload
void unloader(node* current);

// root of our dictionary trie
node* root;

// initialize size of dictionary to make counting easier later
unsigned int dictionary_size = 0;

/**
 * Returns true if word is in dictionary else false.
 */
bool check(const char* word)
{
    // find the length of the word, which is useful for conversion and checking
    int n = strlen(word);

    // set up something to hold the lowercased letters since we may need to change them
    char check;

    // make cursor to go through dictionary
    node* cursor = root;

    // go through each letter of word, make sure it's lower case, make sure it's
    // part of a trie, and check if the end is a word
    for (int i = 0; i < n; i++)
    {
        // lower case what we got
        check = tolower(word[i]);

        // we go through the same structure as in load to check if
        // there is a letter in the trie that is the same
        char c = (check == '\'') ? ALPHABET - 1 : check - 'a';

        // check if node exists and if not then this is not a word,
        // so return false
        if (cursor->children[c] == NULL)
        {
            return false;
        }

        // if node existed, go to it
        else
        {
            cursor = cursor->children[c];
        }
    }

    // if we managed to get through the whole word, return the word
    // at this node
    return cursor->word;
}

/**
 * Loads dictionary into memory.  Returns true if successful else false.
 */
bool load(const char* dictionary)
{
    // open dictionary we got from speller.c
    FILE* file = fopen(dictionary, "r");

    // check if dictionary opened
    if (file == NULL)
    {
        return false;
    }

    // make root for the dictionary
    root = malloc(sizeof(node));
    if (root == NULL)
    {
        fclose(file);
        return false;
    }

    // initialize bool of the root node to avoid memory leak errors
    root->word = false;

    // initialize children of the root
    for (int i = 0; i < ALPHABET; i++)
    {
        root->children[i] = NULL;
    }

    // points cursor at the root
    node* cursor = root;

    // loop through dictionary making the trie
    for (int c = fgetc(file); c != EOF; c = fgetc(file))
    {
        if (c != '\n')
        {
            // change letters to their corresponding numbers,
            // starting from a = 0, special casing apostrophe as 26
            int index = (c == '\'') ? ALPHABET - 1 : c - 'a';

            // check if node exists and if not then make it and go to it
            if (cursor->children[index] == NULL)
            {
                // make node
                cursor->children[index] = calloc(1, sizeof(node));
                if (cursor->children[index] == NULL)
                {
                    unload();
                    fclose(file);
                    return false;
                }
            }

            // go to node, which may have just been made
            cursor = cursor->children[index];
        }
        else
        {
            // if we are at the end of the word, mark it as being a word
            cursor->word = true;

            // return to the root of the dictionary trie
            cursor = root;

            // increment our count of the dictionary size
            dictionary_size++;
        }
    }

    // fail if we encountered errors in reading
    if (ferror(file))
    {
        unload();
        fclose(file);
        return false;
    }

    // close our dictionary when done
    fclose(file);

    return true;
}

/**
 * Returns number of words in dictionary if loaded else 0 if not yet loaded.
 */
unsigned int size(void)
{
    // counting was done when the dictionary was made.
    return dictionary_size;
}

/**
 * Unloads dictionary from memory.  Returns true if successful else false.
 */
bool unload(void)
{
    // enter recursive function
    unloader(root);

    // return when we are successful
    return true;
}

/*
 * checks if we're at the bottom of the trie and if so starts to free malloced
 * memory
 */
void unloader(node* current)
{
    // iterate over all the children to see if they point to anything and go
    // there if they do point
    for (int i = 0; i < ALPHABET; i++)
    {
        if (current->children[i] != NULL)
        {
            unloader(current->children[i]);
        }
    }

    // after we check all the children point to null we can get rid of the node
    // and return to the previous iteration of this function.
    free(current);
}
