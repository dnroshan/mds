/**
 * mds — A micro-display server
 * Copyright © 2014  Mattias Andrée (maandree@member.fsf.org)
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "linked-list.h"

#include <string.h>


/**
 * The default initial capacity
 */
#ifndef LINKED_LIST_DEFAULT_INITIAL_CAPACITY
#define LINKED_LIST_DEFAULT_INITIAL_CAPACITY  128
#endif


/**
 * Computes the nearest, but higher, power of two,
 * but only if the current value is not a power of two
 * 
 * @param   value  The value to be rounded up to a power of two
 * @return         The nearest, but not smaller, power of two
 */
static size_t to_power_of_two(size_t value)
{
  value -= 1;
  value |= value >> 1;
  value |= value >> 2;
  value |= value >> 4;
  value |= value >> 8;
  value |= value >> 16;
#if __WORDSIZE == 64
  value |= value >> 32;
#endif
  return value + 1;
}


/**
 * Create a linked list
 * 
 * @param   this      Memory slot in which to store the new linked list
 * @param   capacity  The minimum initial capacity of the linked list, 0 for default
 * @return            Non-zero on error, `errno` will have been set accordingly
 */
int linked_list_create(linked_list_t* this, size_t capacity)
{
  /* Use default capacity of zero is specified. */
  if (capacity == 0)
    capacity = LINKED_LIST_DEFAULT_INITIAL_CAPACITY;
  
  /* Initialise the linked list. */
  capacity = to_power_of_two(capacity);
  this->capacity = capacity;
  this->edge = 0;
  this->end = 1;
  this->reuse_head = 0;
  this->reusable = NULL;
  this->values = NULL;
  this->next = NULL;
  this->previous = NULL;
  if ((this->reusable = malloc(capacity * sizeof(ssize_t))) == NULL)
    return -1;
  if ((this->values = malloc(capacity * sizeof(size_t))) == NULL)
    return -1;
  if ((this->next = malloc(capacity * sizeof(ssize_t))) == NULL)
    return -1;
  if ((this->previous = malloc(capacity * sizeof(ssize_t))) == NULL)
    return -1;
  this->values[this->edge] = 0;
  this->next[this->edge] = this->edge;
  this->previous[this->edge] = this->edge;
  
  return 0;
}


/**
 * Release all resources in a linked list, should
 * be done even if `linked_list_create` fails
 * 
 * @param  this  The linked list
 */
void linked_list_destroy(linked_list_t* this)
{
  if (this->reusable != NULL)  free(this->reusable);
  if (this->values   != NULL)  free(this->values);
  if (this->next     != NULL)  free(this->next);
  if (this->previous != NULL)  free(this->previous);
  
  this->reusable = NULL;
  this->values   = NULL;
  this->next     = NULL;
  this->previous = NULL;
}


/**
 * Clone a linked list
 * 
 * @param   this  The linked list to clone
 * @param   out   Memory slot in which to store the new linked list
 * @return        Non-zero on error, `errno` will have been set accordingly
 */
int linked_list_clone(linked_list_t* this, linked_list_t* out)
{
  size_t n = this->capacity * sizeof(ssize_t);
  size_t*  new_values;
  ssize_t* new_next;
  ssize_t* new_previous;
  ssize_t* new_reusable;
  
  out->values = NULL;
  out->next = NULL;
  out->previous = NULL;
  out->reusable = NULL;
  
  if ((new_values = malloc(n)) == NULL)
    return -1;
  if ((new_next = malloc(n)) == NULL)
    {
      free(new_values);
      return -1;
    }
  if ((new_previous = malloc(n)) == NULL)
    {
      free(new_values);
      free(new_previous);
      return -1;
    }
  if ((new_reusable = malloc(n)) == NULL)
    {
      free(new_values);
      free(new_previous);
      free(new_reusable);
      return -1;
    }
  
  out->values = new_values;
  out->next = new_next;
  out->previous = new_previous;
  out->reusable = new_reusable;
  
  out->capacity = this->capacity;
  out->end = this->end;
  out->reuse_head = this->reuse_head;
  out->edge = this->edge;
  
  memcpy(out->values, this->values, n);
  memcpy(out->next, this->next, n);
  memcpy(out->previous, this->previous, n);
  memcpy(out->reusable, this->reusable, n);
  
  return 0;
}


/**
 * Pack the list so that there are no reusable
 * positions, and reduce the capacity to the
 * smallest capacity that can be used. Not that
 * values (nodes) returned by the list's methods
 * will become invalid. Additionally (to reduce
 * the complexity) the list will be defragment
 * so that the nodes' indices are continuous.
 * This method has linear time complexity and
 * linear memory complexity.
 * 
 * @param   this  The list
 * @return        Non-zero on error, `errno` will have been set accordingly
 */
int linked_list_pack(linked_list_t* this)
{
  size_t size = this->end - this->reuse_head;
  size_t cap = to_power_of_two(size);
  ssize_t head = 0;
  size_t i = 0;
  ssize_t node;
  size_t* vals;
  
  vals = malloc(cap * sizeof(size_t));
  if (vals == NULL)
    return -1;
  while (((size_t)head != this->end) && (this->next[head] == LINKED_LIST_UNUSED))
    head++;
  if ((size_t)head != this->end)
    for (node = head; (node != head) || (i == 0); i++)
      {
	vals[i] = this->values[node];
	node = this->next[node];
      }
  
  if (cap != this->capacity)
    {
      ssize_t* new_next;
      ssize_t* new_previous;
      ssize_t* new_reusable;
      
      new_next = malloc(cap * sizeof(ssize_t));
      if (new_next == NULL)
	{
	  free(vals);
	  return -1;
	}
      new_previous = malloc(cap * sizeof(ssize_t));
      if (new_previous == NULL)
	{
	  free(vals);
	  free(new_next);
	  return -1;
	}
      new_reusable = malloc(cap * sizeof(ssize_t));
      if (new_reusable == NULL)
	{
	  free(vals);
	  free(new_next);
	  free(new_previous);
	  return -1;
	}
      
      free(this->next);
      free(this->previous);
      free(this->reusable);
      
      this->next = new_next;
      this->previous = new_previous;
      this->reusable = new_reusable;
    }
  
  for (i = 0; i < size; i++)
    this->next[i] = (ssize_t)(i + 1);
  this->next[size - 1] = 0;
  
  for (i = 1; i < size; i++)
    this->previous[i] = (ssize_t)(i - 1);
  this->previous[0] = (ssize_t)(size - 1);
  
  this->values = vals;
  this->end = size;
  this->reuse_head = 0;
  
  return 0;
}


/**
 * Gets the next free position, and grow the
 * arrays if necessary. This methods has constant
 * amortised time complexity.
 * 
 * @param   this  The list
 * @return        The next free position
 */
static ssize_t linked_list_get_next(linked_list_t* this)
{
  if (this->reuse_head > 0)
    return this->reusable[--(this->reuse_head)];
  if (this->end == this->capacity)
    {
      this->capacity <<= 1;
      this->values = realloc(this->values, this->capacity * sizeof(size_t));
      if (this->values == NULL)
	return LINKED_LIST_UNUSED;
      this->next = realloc(this->next, this->capacity * sizeof(ssize_t));
      if (this->next == NULL)
	return LINKED_LIST_UNUSED;
      this->previous = realloc(this->previous, this->capacity * sizeof(ssize_t));
      if (this->previous == NULL)
	return LINKED_LIST_UNUSED;
      this->reusable = realloc(this->reusable, this->capacity * sizeof(ssize_t));
      if (this->reusable == NULL)
	return LINKED_LIST_UNUSED;
    }
  return (ssize_t)(this->end++);
}


/**
 * Mark a position as unused
 * 
 * @param   this  The list
 * @param   node  The position
 * @return        The position
 */
static ssize_t linked_list_unuse(linked_list_t* this, ssize_t node)
{
  if (node < 0)
    return node;
  this->reusable[this->reuse_head++] = node;
  this->next[node] = LINKED_LIST_UNUSED;
  this->previous[node] = LINKED_LIST_UNUSED;
  return node;
}


/**
 * Insert a value after a specified, reference, node
 * 
 * @param   this         The list
 * @param   value        The value to insert
 * @param   predecessor  The reference node
 * @return               The node that has been created and inserted,
 *                       `LINKED_LIST_UNUSED` on error, `errno` will be set accordingly
 */
ssize_t linked_list_insert_after(linked_list_t* this, size_t value, ssize_t predecessor)
{
  ssize_t node = linked_list_get_next(this);
  this->values[node] = value;
  this->next[node] = this->next[predecessor];
  this->next[predecessor] = node;
  this->previous[node] = predecessor;
  this->previous[this->next[node]] = node;
  return node;
}


/**
 * Remove the node after a specified, reference, node
 * 
 * @param   this         The list
 * @param   predecessor  The reference node
 * @return               The node that has been removed
 */
ssize_t linked_list_remove_after(linked_list_t* this, ssize_t predecessor)
{
  ssize_t node = this->next[predecessor];
  this->next[predecessor] = this->next[node];
  this->previous[this->next[node]] = predecessor;
  return linked_list_unuse(this, node);
}


/**
 * Insert a value before a specified, reference, node
 * 
 * @param   this       The list
 * @param   value      The value to insert
 * @param   successor  The reference node
 * @return             The node that has been created and inserted,
 *                     `LINKED_LIST_UNUSED` on error, `errno` will be set accordingly
 */
ssize_t linked_list_insert_before(linked_list_t* this, size_t value, ssize_t successor)
{
  ssize_t node = linked_list_get_next(this);
  this->values[node] = value;
  this->previous[node] = this->next[successor];
  this->previous[successor] = node;
  this->next[node] = successor;
  this->next[this->previous[node]] = node;
  return node;
}


/**
 * Remove the node before a specified, reference, node
 * 
 * @param   this       The list
 * @param   successor  The reference node
 * @return             The node that has been removed
 */
ssize_t linked_list_remove_before(linked_list_t* this, ssize_t successor)
{
  ssize_t node = this->previous[successor];
  this->previous[successor] = this->previous[node];
  this->next[this->previous[node]] = successor;
  return linked_list_unuse(this, node);
}


/**
 * Remove the node from the list
 * 
 * @param  this  The list
 * @param  node  The node to remove
 */
void linked_list_remove(linked_list_t* this, ssize_t node)
{
  this->next[this->previous[node]] = this->next[node];
  this->previous[this->next[node]] = this->previous[node];
  linked_list_unuse(this, node);
}

