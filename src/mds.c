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
#include "mds.h"
#include "config.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>


/**
 * Number of elements in `argv`
 */
static int argc;

/**
 * Command line arguments
 */
static const char** argv;


/**
 * Entry point of the program
 * 
 * @param   argc_  Number of elements in `argv_`
 * @param   argv_  Command line arguments
 * @return         Non-zero on error
 */
int main(int argc_, const char** argv_)
{
  struct sockaddr_un address;
  char pathname[PATH_MAX];
  char piddata[64];
  unsigned int display;
  int fd;
  FILE *f;
  
  
  argc = argc_;
  argv = argv_;
  
  
  /* Sanity check the number of command line arguments. */
  if (argc > ARGC_LIMIT)
    {
      fprintf(stderr,
	      "%s: that number of arguments is ridiculous, I will not allow it.\n",
	      *argv);
      return 1;
    }
  
  /* Stymied if the effective user is not root. */
  if (geteuid() != ROOT_USER_UID)
    {
      fprintf(stderr,
	      "%s: the effective user is not root, cannot continue.\n",
	      *argv);
      return 1;
    }
  
  /* Create directory for socket files, PID files and such. */
  if (create_runtime_root_directory())
    return 1;
  
  /* Determine display index. */
  for (display = 0; display < DISPLAY_MAX; display++)
    {
      snprintf(pathname, sizeof(pathname) / sizeof(char), "%s/%u.pid",
	       MDS_RUNTIME_ROOT_DIRECTORY, display);
      
      fd = open(pathname, O_CREAT | O_EXCL);
      if (fd == -1)
	{
	  /* Reuse display index not no longer used. */
	  size_t read_len;
	  f = fopen(pathname, "r");
	  if (f == NULL) /* Race, or error? */
	    {
	      perror(*argv);
	      continue;
	    }
	  read_len = fread(piddata, 1, sizeof(piddata) / sizeof(char), f);
	  if (ferror(f)) /* Failed to read. */
	    perror(*argv);
	  else if (feof(f) == 0) /* Did not read everything. */
	    fprintf(stderr,
		    "%s: the content of a PID file is longer than expected.\n",
		    *argv);
	  else
	    {
	      pid_t pid = 0;
	      size_t i, n = read_len - 1;
	      for (i = 0; i < n; i++)
		{
		  char c = piddata[i];
		  if (('0' <= c) && (c <= '9'))
		    pid = pid * 10 + (c & 15);
		  else
		    {
		      fprintf(stderr,
			      "%s: the content of a PID file is invalid.\n",
			      *argv);
		      goto bad;
		    }
		}
	      if (piddata[n] != '\n')
		{
		  fprintf(stderr,
			  "%s: the content of a PID file is invalid.\n",
			  *argv);
		  goto bad;
		}
	      if (kill(pid, 0) < 0) /* Check if the PID is still allocated to any process. */
		if (errno == ESRCH) /* PID is not used. */
		  {
		    fclose(f);
		    close(fd);
		    break;
		  }
	    }
	bad:
	  fclose(f);
	  continue;
	}
      close(fd);
      break;
    }
  if (display == DISPLAY_MAX)
    {
      fprintf(stderr,
	      "%s: Sorry, too many displays on the system.\n",
	      *argv);
      return 1;
      /* Yes, the directory could have been removed, but it probably was not. */
    }
  
  /* Create PID file. */
  f = fopen(pathname, "w");
  if (f == NULL)
    {
      perror(*argv);
      return 1;
    }
  snprintf(piddata, sizeof(piddata) / sizeof(char), "%u\n", getpid());
  if (fwrite(piddata, 1, strlen(piddata), f) < strlen(piddata))
    {
      fclose(f);
      if (unlink(pathname) < 0)
	perror(*argv);
      return -1;
    }
  fflush(f);
  fclose(f);
  
  /* Save MDS_DISPLAY environment variable. */
  snprintf(pathname, sizeof(pathname) / sizeof(char), /* Excuse the reuse without renaming. */
	   "%s=%u", DISPLAY_ENV, display);
  putenv(pathname);
  
  /* Create display socket. */
  snprintf(pathname, sizeof(pathname) / sizeof(char), "%s/%u.socket",
	   MDS_RUNTIME_ROOT_DIRECTORY, display);
  address.sun_family = AF_UNIX;
  strcpy(address.sun_path, pathname);
  unlink(pathname);
  fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if ((fchmod(fd, S_IRWXU) < 0) ||
      (fchown(fd, getuid(), NOBODY_GROUP_GID) < 0))
    {
      perror(*argv);
      close(fd);
      return 1;
    }
  if (bind(fd, (struct sockaddr*)(&address), sizeof(address)) < 0)
    {
      perror(*argv);
      close(fd);
      return 1;
    }
  
  /* Start listening on socket. */
  if (listen(fd, SOMAXCONN) < 0)
    {
      perror(*argv);
      close(fd);
      return 1;
    }
  
  /* Drop privileges. They most not be propagated non-authorised components. */
  /* setgid should not be set, but just to be safe we are restoring both user and group. */
  if ((seteuid(getuid()) < 0) || (setegid(getgid()) < 0))
    {
      perror(*argv);
      close(fd);
      return 1;
    }
  
  return 0;
}


/**
 * Create directory for socket files, PID files and such
 * 
 * @return  Non-zero on error
 */
int create_runtime_root_directory(void)
{
  struct stat attr;
  
  if (stat(MDS_RUNTIME_ROOT_DIRECTORY, &attr) == 0)
    {
      /* Cannot create the directory, its pathname refers to an existing. */
      if (S_ISDIR(attr.st_mode) == 0)
	{
	  /* But it is not a directory so we cannot continue. */
	  fprintf(stderr,
		  "%s: %s already exists but is not a directory.\n",
		  MDS_RUNTIME_ROOT_DIRECTORY, *argv);
	  return 1;
	}
    }
  else
    {
      /* Directory is missing, create it. */
      if (mkdir(MDS_RUNTIME_ROOT_DIRECTORY, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) < 0)
	{
	  if (errno != EEXIST) /* Unlikely race condition. */
	    {
	      perror(*argv);
	      return 1;
	    }
	}
      else
	/* Set ownership. */
	if (chown(MDS_RUNTIME_ROOT_DIRECTORY, ROOT_USER_UID, ROOT_GROUP_GID) < 0)
	  {
	    perror(*argv);
	    return 1;
	  }
    }
  
  return 0;
}

