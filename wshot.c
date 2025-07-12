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
#include "./xdg-output-unstable-v1.h"

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

#ifndef SYS_memfd_create
#define SYS_memfd_create 319
#endif

#define MIN(a, b) ((a) < (b) ? (a) : (b))

int ok[5] = {0};
int sing_ok = 0;

const char *help_message = "wshot v0.1.0\n"
                           "Usage: wshot [ARGUMENTS]\n\n"
                           "Arguments:\n"
                           "-o: Output file name.\n"
                           "-r: Screenshot region (slurp)\n"
                           "-c: Hide cursor.\n"
                           "-m: Select specific monitor.\n"
                           "-s: use with wl-copy: wshot -s | wl-copy\n"
                           "-w: delay before screenshot\n";



struct Output {
  struct wl_list link;
  struct wl_output *output;
  struct zxdg_output_v1 *xoutput;
  int logical_x;
  int logical_y;
  int logical_w;
  int logical_h;
  char* name;
};

struct state {
  struct wl_output *output;
  struct wl_shm *shm;
  struct zwlr_screencopy_manager_v1 *scrcopy;
  struct zxdg_output_manager_v1 *xdg_output_manager;
  struct wl_list outputs;
  struct zxdg_output_v1 *xdg_output;
  struct Output *current_output;
};

struct Opts {
  char* output;
  int region;
  char* region_string;
  char* output_name;
  int cursor;
  int wait;
  int stdout;
};

struct Opts opts;

struct frame_d {
  int id;
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
int output_count;


void reg_glob(
	      void *data, struct wl_registry *registry,
	      uint32_t id, const char *interface, uint32_t version
	      ) {
  if (strcmp(interface, "wl_output") == 0) {
    struct Output *out = calloc(1, sizeof(struct Output));
    out->output = wl_registry_bind(registry, id, &wl_output_interface, MIN(version, 4));
    wl_list_insert(&st.outputs, &out->link);
  } else if (strcmp(interface, "wl_shm") == 0) {
    st.shm = wl_registry_bind(registry, id, &wl_shm_interface, version);
  } else if (strcmp(interface, "zwlr_screencopy_manager_v1") == 0) {
    st.scrcopy = wl_registry_bind(registry, id, &zwlr_screencopy_manager_v1_interface, version);
  } else if (strcmp(interface, "zxdg_output_manager_v1") == 0) {
    st.xdg_output_manager = wl_registry_bind(registry, id, &zxdg_output_manager_v1_interface, version);
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

void save_png(struct frame_d **frames, int count) {
  int total_w = 0;
  int max_h = 0;
  if (opts.output == NULL) {
    opts.output = "-";
  }

  
  if (!frames || count <= 0) {
    fprintf(stderr, "Invalid frames array or count\n");
    return;
  }

  for (int i = 0; i < count; i++) {
    if (!frames[i]) {
      fprintf(stderr, "Frame %d is NULL\n", i);
      continue;
    }
    total_w += frames[i]->width;
    if (frames[i]->height > max_h) {
      max_h = frames[i]->height;
    }
  }
  
  if (total_w == 0 || max_h == 0 || max_h > 10000) {
    fprintf(stderr, "Invalid image dimensions: width=%u height=%u\n", total_w, max_h);
    return;
  }

  FILE *file;
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
    if (file && file != stdout) fclose(file);
    return;
  }
  png_infop info_ptr = png_create_info_struct(png_ptr);
  if (!info_ptr) {
    fprintf(stderr, "Unable to create png info struct.");
    png_destroy_write_struct(&png_ptr, NULL);
    if (file && file != stdout) fclose(file);
    return;
  }
  
  if (setjmp(png_jmpbuf(png_ptr))) {
    fprintf(stderr, "Failed to create png file.");
    png_destroy_write_struct(&png_ptr, &info_ptr);
    if (file && file != stdout) fclose(file);
    return;
  }
  png_init_io(png_ptr, file);

  png_set_compression_level(png_ptr, Z_DEFAULT_COMPRESSION);
  png_set_IHDR(png_ptr, info_ptr, total_w, max_h, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
  png_write_info(png_ptr, info_ptr);
  
  for (int y = 0; y < max_h; y++) {
    png_bytep row_pointers = malloc(total_w * 3);
    if (!row_pointers) {
      fprintf(stderr, "Failed to allocate row buffer\n");
      png_destroy_write_struct(&png_ptr, &info_ptr);
      if (file && file != stdout) fclose(file);
      return;
    }
    
    int x_off = 0;

    for (int i = 0; i < count; i++) {
      struct frame_d *framed = frames[i];
      
      if (!framed) {
        fprintf(stderr, "Frame %d is NULL\n", i);
        if (i < count - 1) {
          continue;
        }
        break;
      }
      
      if (!framed->data || framed->data == MAP_FAILED) {
        fprintf(stderr, "Invalid frame data at index %d\n", i);
        memset(&row_pointers[x_off * 3], 0, framed->width * 3);
        x_off += framed->width;
        continue;
      }
      
 
      if (framed->sd <= 0 || framed->sd < framed->width * 4) {
        fprintf(stderr, "Invalid stride %d at frame %d (width=%d)\n", framed->sd, i, framed->width);
        memset(&row_pointers[x_off * 3], 0, framed->width * 3);
        x_off += framed->width;
        continue;
      }
      
      uint32_t *pixels = (uint32_t *)framed->data;
      int row_stride = framed->sd / 4;
      
      size_t expected_size = framed->height * framed->sd;
      if (framed->size < expected_size) {
        fprintf(stderr, "Frame %d size mismatch: expected %zu, got %zu\n", i, expected_size, framed->size);
        memset(&row_pointers[x_off * 3], 0, framed->width * 3);
        x_off += framed->width;
        continue;
      }
      
      if (y >= framed->height) {
        memset(&row_pointers[x_off * 3], 0, framed->width * 3);
        x_off += framed->width;
        continue;
      }
      
      for (int x = 0; x < framed->width; x++) {
        int pixel_idx = y * row_stride + x;
        
        if (pixel_idx >= (int)(framed->height * row_stride)) {
          fprintf(stderr, "Pixel index out of bounds at frame %d: idx=%d, max=%d\n", 
                  i, pixel_idx, (int)(framed->height * row_stride));
          row_pointers[(x_off + x) * 3 + 0] = 0;
          row_pointers[(x_off + x) * 3 + 1] = 0;
          row_pointers[(x_off + x) * 3 + 2] = 0;
          continue;
        }
        
        uint32_t pixel = pixels[pixel_idx];
        
        int out_idx = (x_off + x) * 3;
        if (out_idx + 2 >= total_w * 3) {
          fprintf(stderr, "Output buffer overflow at frame %d, x=%d\n", i, x);
          break;
        }
        
        row_pointers[out_idx + 0] = (pixel >> 16) & 0xFF; // R
        row_pointers[out_idx + 1] = (pixel >> 8) & 0xFF;  // G
        row_pointers[out_idx + 2] = pixel & 0xFF;         // B
      }
      x_off += framed->width;
    }
    
    png_write_row(png_ptr, row_pointers);
    free(row_pointers);
  }
  
  png_write_end(png_ptr, NULL);
  png_destroy_write_struct(&png_ptr, &info_ptr);
  
  if (file != stdout) {
    fclose(file);
    printf("Screenshot saved successfully\n");
  }
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
  // clean(framed);
  ok[framed->id] = 1;
  zwlr_screencopy_frame_v1_destroy(frame);
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

void log_pos(void *data,
	     struct zxdg_output_v1 *xdg_output, int32_t x, int32_t y) {
  struct Output *out = data;
  out->logical_x = x;
  out->logical_y = y;
  // printf("posfunc: w: %d, h: %d x: %d y: %d ", out->logical_w, out->logical_h, out->logical_x, out->logical_y);
}

void log_siz(void *data,
	      struct zxdg_output_v1 *xdg_output, int32_t w, int32_t h) {
  struct Output *out = data;
  out->logical_w = w;
  out->logical_h = h;
  // printf("sizfunc: w: %d, h: %d x: %d y: %d ", out->logical_w, out->logical_h, out->logical_x, out->logical_y);
}

void handle_done(void* data, struct zxdg_output_v1 *xdg_out) {}

void handle_name(void *data,
		 struct zxdg_output_v1 *xdg_output, const char *name) {
  struct Output *out = data;
  out->name = strdup(name);
  if (out->name == NULL) {
    fprintf(stderr, "strdup memory allocation error.");
    return; 
  }
}
void handle_des(void *data,
		struct zxdg_output_v1 *xdg_output, const char *name) {}
struct zxdg_output_v1_listener xdg_listener = {
  .logical_position = log_pos,
  .logical_size = log_siz,
  .done = handle_done,
  .name = handle_name,
  .description = handle_des,
};

struct wl_display *display = NULL;
struct wl_registry *registry = NULL;

struct Output* monitor_index(int x, int y, int w, int h) {
  struct Output* out;
  wl_list_for_each(out, &st.outputs, link) {
    if (x >= out->logical_x && y >= out->logical_y &&
	x < out->logical_x + out->logical_w &&
	y < out->logical_y + out->logical_h) {
      return out;
    }
  }

  return NULL;
} 


void capture_screenshot() {
    
  struct Output *out = wl_container_of(st.outputs.next, out, link);
  st.current_output = out;
  if (output_count > 5) output_count = 5;
  if (st.xdg_output_manager) {
    struct Output *out;
    wl_list_for_each(out, &st.outputs, link) {
      out->xoutput = zxdg_output_manager_v1_get_xdg_output(st.xdg_output_manager, out->output);
      zxdg_output_v1_add_listener(out->xoutput, &xdg_listener, out);
      output_count++;
    }
    wl_display_roundtrip(display);
  }

  if (!st.current_output || !st.scrcopy || !st.shm || !st.current_output->output) {
    fprintf(stderr, "Could not found interfaces.");
    return;
  }
  
  struct zwlr_screencopy_frame_v1 *shot;
  struct frame_d *frames[5] = {0};
  int i = 0;
  if (opts.region == 1 && opts.region_string) {
    int x, y, w, h;
    if (sscanf(opts.region_string, "%d,%d %dx%d", &x, &y, &w, &h) == 4) {
      output_count = 1;
      struct Output *outputr = monitor_index(x, y, w, h);
      if (!outputr) {
	fprintf(stderr, "Region is cannot be found.\n");
	return;
      }
      
      int r_x = x - outputr->logical_x;
      int r_y = y - outputr->logical_y;
      struct frame_d *framed = calloc(1, sizeof(struct frame_d));
      if (!framed) {
	fprintf(stderr, "Memory allocation failed\n");
	return;
      }
      
      framed->id = 0;
      framed->fd = -1;
      
      if (opts.wait > 0) {
	sleep(opts.wait);
      }
      
      struct zwlr_screencopy_frame_v1 *shot = 
	zwlr_screencopy_manager_v1_capture_output_region(
							 st.scrcopy, opts.cursor, outputr->output, 
							 r_x, r_y, w, h);
      if (!shot) {
	fprintf(stderr, "Region capture failed\n");
	free(framed);
	return;
      }
      
      zwlr_screencopy_frame_v1_add_listener(shot, &frame_l, framed);
      frames[0] = framed;
      i = 1;
    } else {
      fprintf(stderr, "Invalid region format: %s\n", opts.region_string);
      return;
    }
  }
  else {
    if (opts.output_name && strlen(opts.output_name) > 0) {
      int found = 0;
      wl_list_for_each(out, &st.outputs, link) {
        if (out->name && strcmp(out->name, opts.output_name) == 0) {
          found = 1;
          output_count = 1;
          struct frame_d *framed = calloc(1, sizeof(struct frame_d));
          if (!framed) {
            fprintf(stderr, "Memory allocation failed\n");
            return;
          }
          memset(framed, 0, sizeof(struct frame_d));
          framed->id = 0;
          framed->fd = -1;
          
          if (opts.wait > 0) {
            sleep(opts.wait);
          }
          
          st.current_output = out;
          shot = zwlr_screencopy_manager_v1_capture_output(st.scrcopy, opts.cursor, out->output);
          if (!shot) {
            fprintf(stderr, "capture_output failed for output %s\n", opts.output_name);
            free(framed);
            return;
          }
          zwlr_screencopy_frame_v1_add_listener(shot, &frame_l, framed);
          frames[0] = framed;
          sing_ok = 1;
          break;
        }
      }
      
      if (!found) {
        fprintf(stderr, "Output '%s' not found\n", opts.output_name);
        return;
      }
    } else {
	  
      wl_list_for_each(out, &st.outputs, link) {
	if (i >= output_count) break;
	struct frame_d *framed = calloc(1, sizeof(struct frame_d));
	if (!framed) {
	  fprintf(stderr, "Memory allocation failed for frame %d\n", i);
	  continue;
	}
	memset(framed, 0, sizeof(struct frame_d));
	framed->id = i;
	framed->fd = -1;
	
	if (opts.wait > 0) {
	  sleep(opts.wait);
	}
	
	shot = zwlr_screencopy_manager_v1_capture_output(st.scrcopy, opts.cursor, out->output);
	if (!shot) {
	  fprintf(stderr, "capture_output failed for output %d\n", i);
	  free(framed);
	  continue;
	}
	zwlr_screencopy_frame_v1_add_listener(shot, &frame_l, framed);
	frames[i] = framed;
	i++;
      }
    }
  }
  
  
  // zwlr_screencopy_frame_v1_add_listener(shot, &frame_l, framed);
  while (1) {
    int done = 1;
    for (int i = 0;i < output_count; i++) {
      if (!ok[i]) {
	done = 0;
	break;
      }
    }

    if (done) break;
    if (wl_display_dispatch(display) == -1) {
      fprintf(stderr, "dispatch error\n");
      return;
    }
  }

  save_png(frames, output_count);

  for (int i = 0; i < output_count; i++) {
    if (frames[i]) {
      clean(frames[i]);
      free(frames[i]);
    }
  }
}


int main (int argc, char* argv[]) {
  output_count = 0;
  opts.output_name = "";
  opts.region = 0;
  memset(&st, 0, sizeof(st));
  display = wl_display_connect(NULL);
  if (!display) {
    fprintf(stderr, "Unable to connect wayland display.");
    return 1;
  }

  registry = wl_display_get_registry(display);
  wl_list_init(&st.outputs);
  wl_registry_add_listener(registry, &reg_listener, NULL);
  
  wl_display_roundtrip(display);
  
  if (!st.shm) {
    fprintf(stderr, "Error: st.shm is NULL after roundtrip!\n");
    wl_display_disconnect(display);
    return 1;
  }

  if (wl_list_empty(&st.outputs)) {
    fprintf(stderr, "No outputs found in registry!\n");
    return 1;
  }

  //  int list_lenght = wl_list_length(&st.outputs);

  // printf("List Lenght : %d\n", list_lenght);

  
  opts.cursor = 1;
  opts.wait = 0;
  opts.stdout = 0;
  opts.output = "screenshot.png";
  int opt;
  int x, y, w, h;
  while((opt = getopt(argc, argv, "sho:r:m:w:c")) != -1) {
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
    case 'r':
      opts.region = 1;
      opts.region_string = optarg;
      break;
    case 's':
      opts.stdout = 1;
      break;
    case 'w':
      opts.wait = atoi(optarg);
      break;
    case 'm':
      opts.output_name = optarg;
      break;
    }
  }
  
  capture_screenshot();
  
  
  wl_display_disconnect(display);
  return 0; 
}
