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
#include "client-list.h"

#include "macros.h"

#include <string.h>
#include <errno.h>


/**
 * The default initial capacity
 */
#ifndef CLIENT_LIST_DEFAULT_INITIAL_CAPACITY
#define CLIENT_LIST_DEFAULT_INITIAL_CAPACITY  8
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
 * Create a client list
 * 
 * @param   this      Memory slot in which to store the new client list
 * @param   capacity  The minimum initial capacity of the client list, 0 for default
 * @return            Non-zero on error, `errno` will have been set accordingly
 */
int client_list_create(client_list_t* restrict this, size_t capacity)
{
  /* Use default capacity of zero is specified. */
  if (capacity == 0)
    capacity = CLIENT_LIST_DEFAULT_INITIAL_CAPACITY;
  
  /* Initialise the client list. */
  this->capacity = capacity = to_power_of_two(capacity);
  this->size     = 0;
  this->clients  = NULL;
  if (xmalloc(this->clients, capacity, uint64_t))
    return -1;
  
  return 0;
}


/**
 * Release all resources in a client list, should
 * be done even if `client_list_create` fails
 * 
 * @param  this  The client list
 */
void client_list_destroy(client_list_t* restrict this)
{
  free(this->clients);
  this->clients = NULL;
}


/**
 * Clone a client list
 * 
 * @param   this  The client list to clone
 * @param   out   Memory slot in which to store the new client list
 * @return        Non-zero on error, `errno` will have been set accordingly
 */
int client_list_clone(const client_list_t* restrict this, client_list_t* restrict out)
{
  size_t n = this->capacity * sizeof(uint64_t);
  uint64_t* restrict new_clients = NULL;
  int saved_errno;
  
  out->clients = NULL;
  
  fail_if ((new_clients = malloc(n)) == NULL);
  
  out->clients = new_clients;
  
  out->capacity = this->capacity;
  out->size     = this->size;
  
  memcpy(out->clients, this->clients, n);
  
  return 0;
  
 fail:
  saved_errno = errno;
  free(new_clients);
  return errno = saved_errno, -1;
}


/**
 * Add a client to the list
 * 
 * @param   this    The list
 * @param   client  The client to add
 * @return          Non-zero on error, `errno` will be set accordingly
 */
int client_list_add(client_list_t* restrict this, uint64_t client)
{
  if (this->size == this->capacity)
    {
      uint64_t* old = this->clients;
      if (xrealloc(old, this->capacity <<= 1, uint64_t))
	{
	  this->capacity >>= 1;
	  this->clients = old;
	  return -1;
	}
    }
  
  this->clients[this->size++] = client;
  return 0;
}


/**
 * Remove a client from the list, once
 * 
 * @param  this    The list
 * @param  client  The client to remove
 */
void client_list_remove(client_list_t* restrict this, uint64_t client)
{
  size_t i;
  for (i = 0; i < this->size; i++)
    {
      if (this->clients[i] == client)
	{
	  size_t n = (--(this->size) - i) * sizeof(uint64_t);
	  memmove(this->clients + i, this->clients + i + 1, n);
	  
	  if (this->size << 1 <= this->capacity)
	    {
	      uint64_t* old = this->clients;
	      if (xrealloc(old, this->capacity >>= 1, uint64_t))
		{
		  this->capacity <<= 1;
		  this->clients = old;
		}
	    }
	  
	  return;
	}
    }
}


/**
 * Calculate the buffer size need to marshal a client list
 * 
 * @param   this  The list
 * @return        The number of bytes to allocate to the output buffer
 */
size_t client_list_marshal_size(const client_list_t* restrict this)
{
  return 2 * sizeof(size_t) + this->size * sizeof(uint64_t) + sizeof(int);
}


/**
 * Marshals a client list
 * 
 * @param  this  The list
 * @param  data  Output buffer for the marshalled data
 */
void client_list_marshal(const client_list_t* restrict this, char* restrict data)
{
  buf_set_next(data, int, CLIENT_LIST_T_VERSION);
  buf_set_next(data, size_t, this->capacity);
  buf_set_next(data, size_t, this->size);
  
  memcpy(data, this->clients, this->size * sizeof(uint64_t));
}


/**
 * Unmarshals a client list
 * 
 * @param   this  Memory slot in which to store the new client list
 * @param   data  In buffer with the marshalled data
 * @return        Non-zero on error, `errno` will be set accordingly.
 *                Destroy the list on error.
 */
int client_list_unmarshal(client_list_t* restrict this, char* restrict data)
{
  size_t n;
  
  /* buf_get(data, int, 0, CLIENT_LIST_T_VERSION); */
  buf_next(data, int, 1);
  
  this->clients = NULL;
  
  buf_get_next(data, size_t, this->capacity);
  buf_get_next(data, size_t, this->size);
  
  n = this->capacity * sizeof(uint64_t);
  
  if ((this->clients = malloc(n)) == NULL)
    return -1;
  
  n = this->size * sizeof(uint64_t);
  
  memcpy(this->clients, data, n);
  
  return 0;
}

