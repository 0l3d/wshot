#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <png.h>
#include <zlib.h>
#include <sys/syscall.h>
#include <wayland-client.h>
#include <linux/memfd.h>
#include "./wlr-screencopy-unstable-v1.h"

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

#ifndef SYS_memfd_create
#define SYS_memfd_create 319
#endif

int ok = 0;

const char *help_message = "wshot v0.1.0\n"
                           "Usage: wshot [ARGUMENTS]\n\n"
                           "Arguments:\n"
                           "-o: Output file name.\n"
                           "-r: Screenshot region (slurp)\n"
                           "-c: Hide cursor.\n";


struct state {
  struct wl_output *output;
  struct wl_shm *shm;
  struct zwlr_screencopy_manager_v1 *scrcopy;
};

struct Opts {
  char* output;
  int region;
  char* region_string;
  int cursor;
  int wait;
  int stdout;
};

struct Opts opts;

struct frame_d {
  uint32_t width;
  uint32_t height;
  uint32_t sd;
  uint32_t format;
  struct wl_buffer *buffer;
  void *data;
  size_t size;
  int fd;
};

struct state st;


void reg_glob(
	      void *data, struct wl_registry *registry,
	      uint32_t id, const char *interface, uint32_t version
	      ) {
  if (strcmp(interface, "wl_output") == 0) {
    st.output = wl_registry_bind(registry, id, &wl_output_interface, version);
  } else if (strcmp(interface, "wl_shm") == 0) {
    st.shm = wl_registry_bind(registry, id, &wl_shm_interface, version);
  } else if (strcmp(interface, "zwlr_screencopy_manager_v1") == 0) {
    st.scrcopy = wl_registry_bind(registry, id, &zwlr_screencopy_manager_v1_interface, version);
  }
}

void reg_glob_remove(
		     void *data,
		     struct wl_registry *registry,
		     uint32_t name
		     ) {
  // WHO CARESS??? :))))))))))))))))) bro. REFERANCE : https://github.com/swaywm/wlroots/blob/0855cdacb2eeeff35849e2e9c4db0aa996d78d10/examples/screencopy.c#L167
}

struct wl_registry_listener reg_listener = {
  .global = reg_glob,
  .global_remove = reg_glob_remove,
};

void save_png(struct frame_d *framed) {
  if (opts.output == NULL) {
    opts.output = "-";
  }
  
  FILE *file = fopen(opts.output, "wb");
  if (opts.stdout == 1) {
    file = stdout;
  } else {
    file = fopen(opts.output, "wb");
    if (!file) {
      perror("File could not be opened.");
      return;
    }
 }

 
  png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png_ptr) {
    fprintf(stderr, "Unable to create png write struct.");
    fclose(file);
    return;
  }
  png_infop info_ptr = png_create_info_struct(png_ptr);
  if (!info_ptr) {
    fprintf(stderr, "Unable to create png info struct.");
    png_destroy_write_struct(&png_ptr, NULL);
    fclose(file);
    return;
  }
  
  if (setjmp(png_jmpbuf(png_ptr))) {
    fprintf(stderr, "Failed to create png file.");
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(file);
    return;
  }
  png_init_io(png_ptr, file);

  png_set_compression_level(png_ptr, Z_DEFAULT_COMPRESSION);
  png_set_IHDR(png_ptr, info_ptr, framed->width, framed->height, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE , PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
  png_write_info(png_ptr, info_ptr);
  uint32_t *pixels = (uint32_t *)framed->data;
  png_byte **row_pointers = (png_byte **)malloc(sizeof(png_byte *) * framed->height);
  for (int y = 0; y < framed->height;y++) {
    row_pointers[y] = (png_byte*)malloc(png_get_rowbytes(png_ptr, info_ptr));
    for (int x = 0;x < framed->width;x++) {
      uint32_t pixel = pixels[y * (framed->sd / 4) + x];
      row_pointers[y][x * 3 + 0] = (pixel >> 16) & 0xFF;
      row_pointers[y][x * 3 + 1] = (pixel >> 8) & 0xFF;
      row_pointers[y][x * 3 + 2] = pixel & 0xFF;
    }
  }

  png_write_image(png_ptr, row_pointers);
  png_write_end(png_ptr, NULL);

  for (int i = 0; i < framed->height;i++) {
    free(row_pointers[i]);
  }
  free(row_pointers);

  png_destroy_write_struct(&png_ptr, &info_ptr);
  fclose(file);
  printf("Screenshot saved succesfully");
}


void clean(struct frame_d *framed) {
  if (framed->data && framed->data != MAP_FAILED ) {
    munmap(framed->data, framed->size);
  }

  if (framed->buffer) {
    wl_buffer_destroy(framed->buffer);
  }

  if (framed->fd >= 0) {
    close(framed->fd);
  }
}

void buffer(
	    void *data, struct zwlr_screencopy_frame_v1 *frame,
	    uint32_t format, uint32_t width, uint32_t height,
	    uint32_t sd
	   ) {
  struct frame_d *framed = (struct frame_d *)data;

  framed->width = width;
  framed->height = height;
  framed->format = format;
  framed->sd = sd;
  framed->size = sd * height;

  framed->fd = syscall(SYS_memfd_create, "scrshot", MFD_CLOEXEC);
  if (framed->fd == -1) {
    fprintf(stderr, "memfd failed");
    return;
  }

  if (ftruncate(framed->fd, framed->size) == -1) {
    perror("ftruncate failed");
    close(framed->fd);
    return;
  }
  
  framed->data = mmap(NULL, framed->size, PROT_READ | PROT_WRITE, MAP_SHARED, framed->fd, 0);
  if (framed->data == MAP_FAILED) {
    perror("mmap failed");
    close(framed->fd);
    return;
  }
  
  if (!st.shm) {
    fprintf(stderr, "Error: st.shm is NULL!\n");
    clean(framed);
    return;
  }
  
  struct wl_shm_pool *pool = wl_shm_create_pool(st.shm, framed->fd, framed->size);
  if (!pool) {
    fprintf(stderr, "Failed to create SHM pool\n");
    clean(framed);
    return;
  }
  
  framed->buffer = wl_shm_pool_create_buffer(pool, 0, width, height, sd, format);
  if (!framed->buffer) {
    fprintf(stderr, "Failed to create buffer\n");
    clean(framed);
    return;
  }
  
  wl_shm_pool_destroy(pool);
  zwlr_screencopy_frame_v1_copy(frame, framed->buffer);
}


void ready(
	   void *data, struct zwlr_screencopy_frame_v1 *frame,
	   uint32_t tv_sec_hi, //  REFERANCE: https://github.com/swaywm/wlroots/blob/master/examples/screencopy.c#L133
	   uint32_t tv_sec_lo, uint32_t tv_nsec // REFERANCE: https://github.com/swaywm/wlroots/blob/master/examples/screencopy.c#L133
	   ) {
  struct frame_d *framed = (struct frame_d *)data;
  save_png(framed);
  clean(framed);
  zwlr_screencopy_frame_v1_destroy(frame);
  ok = 1;
}

void failed(void *data, struct zwlr_screencopy_frame_v1 *frame) {
  struct frame_d *framed = (struct frame_d *)data;
  clean(framed);
  zwlr_screencopy_frame_v1_destroy(frame);
}


// UNUSED CALLBACKS
void buffer_done() {}
void damage(void *data, struct zwlr_screencopy_frame_v1 *frame,
            uint32_t x, uint32_t y, uint32_t width, uint32_t height) {}
void linux_dmabuf(void *data, struct zwlr_screencopy_frame_v1 *frame,
                  uint32_t arg1, uint32_t arg2, uint32_t arg3) {}
void flags(void *data, struct zwlr_screencopy_frame_v1 *frame, uint32_t flags) {}
struct zwlr_screencopy_frame_v1_listener frame_l = {
  .buffer = buffer,
  .flags = flags,
  .ready = ready,
  .failed = failed,
  .damage = damage,
  .linux_dmabuf = linux_dmabuf,
  .buffer_done = buffer_done,
};

struct wl_display *display = NULL;
struct wl_registry *registry = NULL;

void capture_screenshot() {
  
  if (!st.output || !st.scrcopy || !st.shm) {
    fprintf(stderr, "Could not found interfaces.");
    wl_display_disconnect(display);
    return;
  }
  
  struct frame_d *framed = malloc(sizeof(struct frame_d));
  memset(framed, 0, sizeof(struct frame_d));
  framed->fd = -1;
  struct zwlr_screencopy_frame_v1 *shot;
  sleep(opts.wait);
  if (opts.region == 1) {
    int x, y, w, h;
    if (sscanf(opts.region_string, "%d,%d %dx%d", &x, &y, &w, &h) == 4) {
          shot = zwlr_screencopy_manager_v1_capture_output_region(st.scrcopy, opts.cursor, st.output, x, y, w, h);   
    }
  } else {
    shot = zwlr_screencopy_manager_v1_capture_output(st.scrcopy, opts.cursor, st.output);
  }
  
  
  if (!shot) {
    fprintf(stderr, "Failed to capture output\n");
    clean(framed);
    return;
  }
  
  zwlr_screencopy_frame_v1_add_listener(shot, &frame_l, framed);
  while(wl_display_dispatch(display) != -1) {
      if (ok == 1) {
	break;
     }
  }
}

int main (int argc, char* argv[]) {
  opts.region = 0;
  memset(&st, 0, sizeof(st));
  display = wl_display_connect(NULL);
  if (!display) {
    fprintf(stderr, "Unable to connect wayland display.");
    return 1;
  }
  registry = wl_display_get_registry(display);

  wl_registry_add_listener(registry, &reg_listener, NULL);
  
  wl_display_roundtrip(display);
  if (!st.shm) {
    fprintf(stderr, "Error: st.shm is NULL after roundtrip!\n");
    wl_display_disconnect(display);
    return 1;
  }
  opts.cursor = 1;
  opts.wait = 0;
  opts.stdout = 0;
  opts.output = "";
  int opt;
  int x, y, w, h;
  while((opt = getopt(argc, argv, "ho:r:w:cs")) != -1) {
    switch (opt) {
    case 'h':
      printf("%s", help_message);
      return 0;
    case 'c':
      opts.cursor = 0;
      break;
    case 'o':
      opts.output = optarg;
      break;
    case 's':
      opts.stdout = 1;
      break;
    case 'r':
      opts.region = 1;
      opts.region_string = optarg;
      break;
    case 'w':
      opts.wait = atoi(optarg);
    }
    break;
  }
  
  capture_screenshot();

  
  wl_display_disconnect(display);
  return 0; 
}
