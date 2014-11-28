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
#include "mds-kbdc.h"

#include "globals.h"
#include "make-tree.h"
#include "simplify-tree.h"

#include <libmdsserver/macros.h>

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>



/**
 * Compile a keyboard layout file
 * 
 * @param   argc_  The number of elements in `argv_`
 * @param   argv_  The command line arguments
 * @return         Zero on and only one success
 */
int main(int argc_, char** argv_)
{
  mds_kbdc_parsed_t result;
  int fatal;
  
  argc = argc_;
  argv = argv_;
  
  mds_kbdc_parsed_initialise(&result);
  fail_if (parse_to_tree(argv[1], &result) < 0);
  if (fatal = mds_kbdc_parsed_is_fatal(&result), fatal)
    goto stop;
  fail_if (simplify_tree(&result) < 0);
 stop:
  mds_kbdc_tree_print(result.tree, stderr);
  mds_kbdc_parsed_print_errors(&result, stderr);
  mds_kbdc_parsed_destroy(&result);
  return fatal;
  
 pfail:
  xperror(*argv);
  mds_kbdc_parsed_destroy(&result);
  return 1;
}

