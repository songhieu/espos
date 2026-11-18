// A LittleFS image as the board's esp_littlefs reads it (the "spiffs" partition, docs/simulator.md): the same lfs.c
// (managed_components/joltwallet__littlefs), block 4096, read/prog 128, cache 512, lookahead 128 (the framework's
// sdkconfig CONFIG_LITTLEFS_*). Every file under DIR goes in at its path.
//   cc -O2 -I$LFS tools/qemu/mklfs.c $LFS/lfs.c $LFS/lfs_util.c -o build-qemu/mklfs   (tools/qemu/mkimage.py does it)
//   build-qemu/mklfs DIR SIZE OUT.bin
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "lfs.h"

static uint8_t* img;
static int rd(const struct lfs_config* c, lfs_block_t b, lfs_off_t o, void* buf, lfs_size_t n) {
  memcpy(buf, img + b * c->block_size + o, n);
  return 0;
}
static int pr(const struct lfs_config* c, lfs_block_t b, lfs_off_t o, const void* buf, lfs_size_t n) {
  memcpy(img + b * c->block_size + o, buf, n);
  return 0;
}
static int er(const struct lfs_config* c, lfs_block_t b) {
  memset(img + b * c->block_size, 0xff, c->block_size);
  return 0;
}
static int sy(const struct lfs_config* c) { return 0; }

static lfs_t fs;
static int files = 0;

static void add(const char* host, const char* at) {
  DIR* d = opendir(host);
  if (!d) return;
  struct dirent* e;
  while ((e = readdir(d))) {
    if (e->d_name[0] == '.') continue;
    char h[1024], p[1024];
    snprintf(h, sizeof h, "%s/%s", host, e->d_name);
    snprintf(p, sizeof p, "%s/%s", at, e->d_name);
    struct stat st;
    if (stat(h, &st)) continue;
    if (S_ISDIR(st.st_mode)) {
      if (lfs_mkdir(&fs, p) && lfs_mkdir(&fs, p) != LFS_ERR_EXIST) fprintf(stderr, "mkdir %s failed\n", p), exit(1);
      add(h, p);
      continue;
    }
    FILE* f = fopen(h, "rb");
    lfs_file_t lf;
    if (!f || lfs_file_open(&fs, &lf, p, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC)) fprintf(stderr, "%s failed\n", p), exit(1);
    char buf[4096];
    for (size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;)
      if (lfs_file_write(&fs, &lf, buf, n) != (lfs_ssize_t)n) fprintf(stderr, "%s: full\n", p), exit(1);
    lfs_file_close(&fs, &lf);
    fclose(f);
    files++;
  }
  closedir(d);
}

int main(int argc, char** argv) {
  if (argc != 4) return fprintf(stderr, "usage: %s DIR SIZE OUT\n", argv[0]), 2;
  const long size = strtol(argv[2], NULL, 0);
  img = malloc(size);
  memset(img, 0xff, size);
  struct lfs_config c = {0};
  c.read = rd, c.prog = pr, c.erase = er, c.sync = sy;
  c.read_size = 128, c.prog_size = 128, c.block_size = 4096, c.block_count = size / 4096;
  c.cache_size = 512, c.lookahead_size = 128, c.block_cycles = 512;
  if (lfs_format(&fs, &c) || lfs_mount(&fs, &c)) return fprintf(stderr, "format failed\n"), 1;
  add(argv[1], "");
  const lfs_ssize_t used = lfs_fs_size(&fs);
  lfs_unmount(&fs);
  FILE* o = fopen(argv[3], "wb");
  fwrite(img, 1, size, o);
  fclose(o);
  printf("%s: %d files, %ld of %ld KB\n", argv[3], files, (long)used * 4, size / 1024);
  return 0;
}
