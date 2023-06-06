#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>

#define stat xv6_stat  // avoid clash with host struct stat
#include "types.h"
#include "fs.h"
#include "stat.h"
#include "param.h"

#ifndef static_assert
#define static_assert(a, b) do { switch (0) case 0: case (a): ; } while (0)
#endif

#define NINODES 200

// Disk layout:
// [ boot block | sb block | log | inode blocks | free bit map | data blocks ]

int nbitmap = FSSIZE/(BSIZE*8) + 1;
int ninodeblocks = NINODES / IPB + 1;
int nlog = LOGSIZE;
int nmeta;    // Number of meta blocks (boot, sb, nlog, inode, bitmap)
int nblocks;  // Number of data blocks

int fsfd;
struct superblock sb;
char zeroes[BSIZE];
uint freeinode = 1;
uint freeblock;


void balloc(int);
void wsect(uint, void*);
void winode(uint, struct dinode*);
void rinode(uint inum, struct dinode *ip);
void rsect(uint sec, void *buf);
uint ialloc(ushort type);
void iappend(uint inum, void *p, int n);

// convert to intel byte order
ushort
xshort(ushort x)
{
  ushort y;
  uchar *a = (uchar*)&y;
  a[0] = x;
  a[1] = x >> 8;
  return y;
}

uint
xint(uint x)
{
  uint y;
  uchar *a = (uchar*)&y;
  a[0] = x;
  a[1] = x >> 8;
  a[2] = x >> 16;
  a[3] = x >> 24;
  return y;
}

int
main(int argc, char *argv[])
{
  int i, cc, fd;
  uint rootino, inum, off;
  struct dirent de;
  char buf[BSIZE];
  struct dinode din;


  static_assert(sizeof(int) == 4, "Integers must be 4 bytes!");

  if(argc < 2){
    fprintf(stderr, "Usage: mkfs fs.img files...\n");
    exit(1);
  }

  assert((BSIZE % sizeof(struct dinode)) == 0);
  assert((BSIZE % sizeof(struct dirent)) == 0);

  fsfd = open(argv[1], O_RDWR|O_CREAT|O_TRUNC, 0666);
  if(fsfd < 0){
    perror(argv[1]);
    exit(1);
  }

  // 1 fs block = 1 disk sector
  nmeta = 2 + nlog + ninodeblocks + nbitmap;
  nblocks = FSSIZE - nmeta;

  sb.size = xint(FSSIZE);
  sb.nblocks = xint(nblocks);
  sb.ninodes = xint(NINODES);
  sb.nlog = xint(nlog);
  sb.logstart = xint(2);
  sb.inodestart = xint(2+nlog);
  sb.bmapstart = xint(2+nlog+ninodeblocks);

  printf("nmeta %d (boot, super, log blocks %u inode blocks %u, bitmap blocks %u) blocks %d total %d\n",
         nmeta, nlog, ninodeblocks, nbitmap, nblocks, FSSIZE);

  freeblock = nmeta;     // the first free block that we can allocate

  for(i = 0; i < FSSIZE; i++)
    wsect(i, zeroes);

  memset(buf, 0, sizeof(buf));
  memmove(buf, &sb, sizeof(sb));
  wsect(1, buf);

  rootino = ialloc(T_DIR);
  assert(rootino == ROOTINO);

  bzero(&de, sizeof(de));
  de.inum = xshort(rootino);
  strcpy(de.name, ".");
  iappend(rootino, &de, sizeof(de));

  bzero(&de, sizeof(de));
  de.inum = xshort(rootino);
  strcpy(de.name, "..");
  iappend(rootino, &de, sizeof(de));

  for(i = 2; i < argc; i++){
    assert(index(argv[i], '/') == 0);

    if((fd = open(argv[i], 0)) < 0){
      perror(argv[i]);
      exit(1);
    }

    // Skip leading _ in name when writing to file system.
    // The binaries are named _rm, _cat, etc. to keep the
    // build operating system from trying to execute them
    // in place of system binaries like rm and cat.
    if(argv[i][0] == '_')
      ++argv[i];

    inum = ialloc(T_FILE);

    bzero(&de, sizeof(de));
    de.inum = xshort(inum);
    strncpy(de.name, argv[i], DIRSIZ);
    iappend(rootino, &de, sizeof(de));

    while((cc = read(fd, buf, sizeof(buf))) > 0)
      iappend(inum, buf, cc);

    close(fd);
  }

  // fix size of root inode dir
  rinode(rootino, &din);
  off = xint(din.size);
  off = ((off/BSIZE) + 1) * BSIZE;
  din.size = xint(off);
  winode(rootino, &din);

  balloc(freeblock);

  exit(0);
}

void
wsect(uint sec, void *buf)
{
  if(lseek(fsfd, sec * BSIZE, 0) != sec * BSIZE){
    perror("lseek");
    exit(1);
  }
  if(write(fsfd, buf, BSIZE) != BSIZE){
    perror("write");
    exit(1);
  }
}

void
winode(uint inum, struct dinode *ip)
{
  char buf[BSIZE];
  uint bn;
  struct dinode *dip;

  bn = IBLOCK(inum, sb);
  rsect(bn, buf);
  dip = ((struct dinode*)buf) + (inum % IPB);
  *dip = *ip;
  wsect(bn, buf);
}

void
rinode(uint inum, struct dinode *ip)
{
  char buf[BSIZE];
  uint bn;
  struct dinode *dip;

  bn = IBLOCK(inum, sb);
  rsect(bn, buf);
  dip = ((struct dinode*)buf) + (inum % IPB);
  *ip = *dip;
}

void
rsect(uint sec, void *buf)
{
  if(lseek(fsfd, sec * BSIZE, 0) != sec * BSIZE){
    perror("lseek");
    exit(1);
  }
  if(read(fsfd, buf, BSIZE) != BSIZE){
    perror("read");
    exit(1);
  }
}

uint
ialloc(ushort type)
{
  uint inum = freeinode++;
  struct dinode din;

  bzero(&din, sizeof(din));
  din.type = xshort(type);
  din.nlink = xshort(1);
  din.size = xint(0);
  winode(inum, &din);
  return inum;
}

void
balloc(int used)
{
  uchar buf[BSIZE];
  int i;

  printf("balloc: first %d blocks have been allocated\n", used);
  assert(used < BSIZE*8);
  bzero(buf, BSIZE);
  for(i = 0; i < used; i++){
    buf[i/8] = buf[i/8] | (0x1 << (i%8));
  }
  printf("balloc: write bitmap block at sector %d\n", sb.bmapstart);
  wsect(sb.bmapstart, buf);
}

#define min(a, b) ((a) < (b) ? (a) : (b))

// void
// iappend(uint inum, void *xp, int n)
// {
//   char *p = (char*)xp;
//   uint fbn, off, n1;
//   struct dinode din;
//   char buf[BSIZE];
//   uint indirect[NINDIRECT];
//   uint x;

//   rinode(inum, &din);
//   off = xint(din.size);
//   // printf("append inum %d at off %d sz %d\n", inum, off, n);
//   while(n > 0){
//     fbn = off / BSIZE;
//     assert(fbn < MAXFILE);
//     if(fbn < NDIRECT){
//       if(xint(din.addrs[fbn]) == 0){
//         din.addrs[fbn] = xint(freeblock++);
//       }
//       x = xint(din.addrs[fbn]);
//     } else {
//       if(xint(din.addrs[NDIRECT]) == 0){
//         din.addrs[NDIRECT] = xint(freeblock++);
//       }
//       rsect(xint(din.addrs[NDIRECT]), (char*)indirect);
//       if(indirect[fbn - NDIRECT] == 0){
//         indirect[fbn - NDIRECT] = xint(freeblock++);
//         wsect(xint(din.addrs[NDIRECT]), (char*)indirect);
//       }
//       x = xint(indirect[fbn-NDIRECT]);
//     }
//     n1 = min(n, (fbn + 1) * BSIZE - off);
//     rsect(x, buf);
//     bcopy(p, buf + off - (fbn * BSIZE), n1);
//     wsect(x, buf);
//     n -= n1;
//     off += n1;
//     p += n1;
//   }
//   din.size = xint(off);
//   winode(inum, &din);
// }

// void
// iappend(uint inum, void *xp, int n)
// {
//   char *p = (char*)xp;
//   uint fbn, off, n1;
//   struct dinode din;
//   char buf[BSIZE];
//   uint indirect[NINDIRECT];
//   // uint double_indirect[DINDIRECT];
//   // uint triple_indirect[TINDIRECT];
//   uint *double_indirect = (uint *) malloc(sizeof(uint) * DINDIRECT);
//   uint *triple_indirect = (uint *) malloc(sizeof(uint) * TINDIRECT);
  
//   uint x;

//   rinode(inum, &din);
//   off = xint(din.size);
//   while(n > 0){
//     fbn = off / BSIZE;
//     assert(fbn < MAXFILE);
//     if(fbn < NDIRECT){
//       if(xint(din.addrs[fbn]) == 0){
//         din.addrs[fbn] = xint(freeblock++);
//       }
//       x = xint(din.addrs[fbn]);
//     } else if(fbn < (NDIRECT + NINDIRECT)){
//       if(xint(din.addrs[NDIRECT]) == 0){
//         din.addrs[NDIRECT] = xint(freeblock++);
//       }
//       rsect(xint(din.addrs[NDIRECT]), (char*)indirect);
//       if(indirect[fbn - NDIRECT] == 0){
//         indirect[fbn - NDIRECT] = xint(freeblock++);
//         wsect(xint(din.addrs[NDIRECT]), (char*)indirect);
//       }
//       x = xint(indirect[fbn-NDIRECT]);
//     } else if(fbn < (NDIRECT + NINDIRECT + DINDIRECT)){ // double indirect block handling
//       if(xint(din.addrs[NDIRECT+1]) == 0){
//         din.addrs[NDIRECT+1] = xint(freeblock++);
//       }
//       rsect(xint(din.addrs[NDIRECT+1]), (char*)double_indirect);
//       if(double_indirect[(fbn - NDIRECT - NINDIRECT)/NINDIRECT] == 0){
//         double_indirect[(fbn - NDIRECT - NINDIRECT)/NINDIRECT] = xint(freeblock++);
//         wsect(xint(din.addrs[NDIRECT+1]), (char*)double_indirect);
//       }
//       x = xint(double_indirect[(fbn-NDIRECT-NINDIRECT)/NINDIRECT]);
//     } else { // triple indirect block handling
//       if(xint(din.addrs[NDIRECT+2]) == 0){
//         din.addrs[NDIRECT+2] = xint(freeblock++);
//       }
//       rsect(xint(din.addrs[NDIRECT+2]), (char*)triple_indirect);
//       if(triple_indirect[(fbn - NDIRECT - NINDIRECT - DINDIRECT)/DINDIRECT] == 0){
//         triple_indirect[(fbn - NDIRECT - NINDIRECT - DINDIRECT)/DINDIRECT] = xint(freeblock++);
//         wsect(xint(din.addrs[NDIRECT+2]), (char*)triple_indirect);
//       }
//       x = xint(triple_indirect[(fbn-NDIRECT-NINDIRECT-DINDIRECT)/DINDIRECT]);
//     }
//     n1 = min(n, (fbn + 1) * BSIZE - off);
//     rsect(x, buf);
//     bcopy(p, buf + off - (fbn * BSIZE), n1);
//     wsect(x, buf);
//     n -= n1;
//     off += n1;
//     p += n1;
//   }
//   din.size = xint(off);
//   winode(inum, &din);
//   free(double_indirect);
//   free(triple_indirect);
// }


void
iappend(uint inum, void *xp, int n)
{
  char *p = (char*)xp;
  uint fbn, off, n1;
  struct dinode din;
  char buf[BSIZE];
  uint indirect[NINDIRECT];
  // uint double_indirect[DINDIRECT];
  // uint triple_indirect[TINDIRECT];
  uint *double_indirect = (uint *) malloc(sizeof(uint) * DINDIRECT);
  uint *triple_indirect = (uint *) malloc(sizeof(uint) * TINDIRECT);
  
  uint x;

  rinode(inum, &din);
  off = xint(din.size);
  while(n > 0){
    fbn = off / BSIZE;
    assert(fbn < MAXFILE);
    if(fbn < NDIRECT){
      if(xint(din.addrs[fbn]) == 0){
        din.addrs[fbn] = xint(freeblock++);
      }
      x = xint(din.addrs[fbn]);
    } else if(fbn < (NDIRECT + NINDIRECT)){
      if(xint(din.addrs[NDIRECT]) == 0){
        din.addrs[NDIRECT] = xint(freeblock++);
      }
      rsect(xint(din.addrs[NDIRECT]), (char*)indirect);
      if(indirect[fbn - NDIRECT] == 0){
        indirect[fbn - NDIRECT] = xint(freeblock++);
        wsect(xint(din.addrs[NDIRECT]), (char*)indirect);
      }
      x = xint(indirect[fbn-NDIRECT]);
    // double indirect block handling
  } else if(fbn < (NDIRECT + NINDIRECT + DINDIRECT)){ 
    if(xint(din.addrs[NDIRECT+1]) == 0){
      din.addrs[NDIRECT+1] = xint(freeblock++);
    }

    uint index1 = (fbn - NDIRECT - NINDIRECT) / NINDIRECT;
    rsect(xint(double_indirect[index1]), (char*)indirect);

    if(indirect[index1] == 0){
      indirect[index1] = xint(freeblock++);
      wsect(xint(double_indirect[index1]), (char*)indirect);
    }

    x = xint(indirect[(fbn - NDIRECT - NINDIRECT) % NINDIRECT]);

  // triple indirect block handling
   } else { 
    if(xint(din.addrs[NDIRECT+2]) == 0){
      din.addrs[NDIRECT+2] = xint(freeblock++);
    }

    uint index1 = (fbn - NDIRECT - NINDIRECT - DINDIRECT) / (NINDIRECT * NINDIRECT);
    rsect(xint(din.addrs[NDIRECT+2]), (char*)triple_indirect);

    if(triple_indirect[index1] == 0){
      triple_indirect[index1] = xint(freeblock++);
      wsect(xint(din.addrs[NDIRECT+2]), (char*)triple_indirect);
    }

    rsect(xint(triple_indirect[index1]), (char*)double_indirect);
    uint index2 = ((fbn - NDIRECT - NINDIRECT - DINDIRECT) / NINDIRECT) % NINDIRECT;
    if(double_indirect[index2] == 0){
      double_indirect[index2] = xint(freeblock++);
      wsect(xint(triple_indirect[index1]), (char*)double_indirect);
    }

    rsect(xint(double_indirect[index2]), (char*)indirect);
    uint index3 = (fbn - NDIRECT - NINDIRECT - DINDIRECT) % NINDIRECT;
    if(indirect[index3] == 0){
      indirect[index3] = xint(freeblock++);
      wsect(xint(double_indirect[index2]), (char*)indirect);
    }

    x = xint(indirect[index3]);
  }
    n1 = min(n, (fbn + 1) * BSIZE - off);
    rsect(x, buf);
    bcopy(p, buf + off - (fbn * BSIZE), n1);
    wsect(x, buf);
    n -= n1;
    off += n1;
    p += n1;
  }
  din.size = xint(off);
  winode(inum, &din);
  free(double_indirect);
  free(triple_indirect);
}
