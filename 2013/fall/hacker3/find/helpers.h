/****************************************************************************
 * helpers.h
 *
 * Computer Science 50
 * Problem Set 3
 *
 * Helper functions for Problem Set 3.
 ***************************************************************************/
 
#include <cs50.h>

#define LIMIT 65536

/*
 * Returns true if value is in array of n values, else false.
 */

bool 
search(int value, int values[], int n);


/*
 * Sorts array of n values.  Returns true if
 * successful, else false.
 */

bool
sort(int values[], int n);
