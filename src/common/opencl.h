/*
    This file is part of darktable,
    Copyright (C) 2010-2022 darktable developers.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define DT_OPENCL_MAX_PLATFORMS 5
#define DT_OPENCL_MAX_PROGRAMS 256
#define DT_OPENCL_MAX_KERNELS 512
#define DT_OPENCL_EVENTLISTSIZE 256
#define DT_OPENCL_EVENTNAMELENGTH 64
#define DT_OPENCL_MAX_ERRORS 5
#define DT_OPENCL_MAX_INCLUDES 7
#define DT_OPENCL_VENDOR_AMD 4098
#define DT_OPENCL_VENDOR_NVIDIA 4318
#define DT_OPENCL_VENDOR_INTEL 0x8086u
#define DT_OPENCL_CBUFFSIZE 1024

// some pseudo error codes in dt opencl usage
#define DT_OPENCL_DEFAULT_ERROR -999
#define DT_OPENCL_SYSMEM_ALLOCATION -998
#define DT_OPENCL_PROCESS_CL -997
#include "common/darktable.h"

#ifdef HAVE_OPENCL

#include "common/dlopencl.h"
#include "common/dtpthread.h"
#include "common/iop_profile.h"
#include "control/conf.h"

// #pragma GCC diagnostic push
// #pragma GCC diagnostic ignored "-Wcomment"
#include <CL/cl.h>
// #pragma GCC diagnostic

#define ROUNDUP(a, n) ((a) % (n) == 0 ? (a) : ((a) / (n)+1) * (n))

// use per device roundups here
#define ROUNDUPDWD(a, b) dt_opencl_dev_roundup_width(a, b)
#define ROUNDUPDHT(a, b) dt_opencl_dev_roundup_height(a, b)

#define DT_OPENCL_DEFAULT_COMPILE_INTEL ("-cl-fast-relaxed-math")
#define DT_OPENCL_DEFAULT_COMPILE_AMD ("-cl-fast-relaxed-math")
#define DT_OPENCL_DEFAULT_COMPILE_NVIDIA ("-cl-fast-relaxed-math")
#define DT_OPENCL_DEFAULT_COMPILE ("-cl-fast-relaxed-math")
#define DT_CLDEVICE_HEAD ("cldevice_v4_")

typedef enum dt_opencl_memory_t
{
  OPENCL_MEMORY_ADD,
  OPENCL_MEMORY_SUB
} dt_opencl_memory_t;

typedef enum dt_opencl_scheduling_profile_t
{
  OPENCL_PROFILE_DEFAULT,
  OPENCL_PROFILE_MULTIPLE_GPUS,
  OPENCL_PROFILE_VERYFAST_GPU
} dt_opencl_scheduling_profile_t;

/**
 * Accounting information used for OpenCL events.
 */
typedef struct dt_opencl_eventtag_t
{
  cl_int retval;
  cl_ulong timelapsed;
  char tag[DT_OPENCL_EVENTNAMELENGTH];
} dt_opencl_eventtag_t;

typedef enum dt_opencl_tunemode_t
{
  DT_OPENCL_TUNE_NOTHING = 0,
  DT_OPENCL_TUNE_MEMSIZE = 1,
  DT_OPENCL_TUNE_PINNED  = 2
} dt_opencl_tunemode_t;

typedef enum dt_opencl_pinmode_t
{
  DT_OPENCL_PINNING_OFF = 0,
  DT_OPENCL_PINNING_ON = 1,
  DT_OPENCL_PINNING_DISABLED = 2
} dt_opencl_pinmode_t;

/**
 * to support multi-gpu and mixed systems with cpu support,
 * we encapsulate devices and use separate command queues.
 */
typedef struct dt_opencl_device_t
{
  dt_pthread_mutex_t lock;
  cl_device_id devid;
  cl_context context;
  cl_command_queue cmd_queue;
  size_t max_image_width;
  size_t max_image_height;
  cl_ulong max_mem_alloc;
  cl_ulong max_global_mem;
  cl_ulong used_global_mem;
  cl_program program[DT_OPENCL_MAX_PROGRAMS];
  cl_kernel kernel[DT_OPENCL_MAX_KERNELS];
  int program_used[DT_OPENCL_MAX_PROGRAMS];
  int kernel_used[DT_OPENCL_MAX_KERNELS];
  cl_event *eventlist;
  dt_opencl_eventtag_t *eventtags;
  int numevents;
  int eventsconsolidated;
  int maxevents;
  int lostevents;
  int totalevents;
  int totalsuccess;
  int totallost;
  int maxeventslot;
  int nvidia_sm_20;
  const char *vendor;
  const char *name;
  const char *cname;
  const char *options;
  cl_int summary;
  // the benchmark value must not be changed by the user
  float benchmark;
  size_t memory_in_use;
  size_t peak_memory;
  size_t used_available;
  // flags what tuning modes should be used
  int tuneactive; 
  // flags detected errors
  int runtime_error;
  // if set to TRUE darktable will not use OpenCL kernels which contain atomic operations (example bilateral).
  // pixelpipe processing will be done on CPU for the affected modules.
  // useful (only for very old devices) if your OpenCL implementation freezes/crashes on atomics or if
  // they are processed with a bad performance.
  int avoid_atomics;

  // pause OpenCL processing for this number of microseconds from time to time
  int micro_nap;

  // During tiling huge amounts of memory need to be transferred between host and device.
  // For some OpenCL implementations direct memory transfers give a drastic performance penalty,
  // this can often be avoided by using indirect transfers via pinned memory,
  // other devices have more efficient direct memory transfer implementations.
  // We can't predict on solid grounds if a device belongs to the first or second group,
  // also pinned mem transfer requires slightly more ram. 
  // this holds a bitmask defined by dt_opencl_pinmode_t
  // the device specific conf key might hold
  // 0 -> disabled by default; might be switched on by tune for performance
  // 1 -> enabled by default
  // 2 -> disabled under all circumstances. This could/should be used if we give away / ship specific keys for buggy systems 
  int pinned_memory;

  // in OpenCL processing round width/height of global work groups to a multiple of these values.
  // reasonable values are powers of 2. this parameter can have high impact on OpenCL performance.
  int clroundup_wd;
  int clroundup_ht;

  // A bitfield that identifies the type of OpenCL device required to test for on-CPU and more.
  unsigned int cltype;

  // This defines how often should dt_opencl_events_get_slot do a dt_opencl_events_flush.
  // It should definitely le lower than the number of events that can be handled by the device/driver.
  // FIXME we should be able to test for that with using >= OpenCl 2.0
  int event_handles;

  // opencl_events enabled for the device, set internally via event_handles
  int use_events;

  // async pixelpipe mode for device
  // if set to TRUE OpenCL pixelpipe will not be synchronized on a per-module basis. this can improve pixelpipe latency.
  // however, potential OpenCL errors would be detected late; in such a case the complete pixelpipe needs to be reprocessed
  // instead of only a single module. export pixelpipe will always be run synchronously.
  int asyncmode;

  // a device might be turned off by force by setting this value to 1
  // also used for blacklisted drivers
  int disabled;

  // Some devices are known to be unused by other apps so there is no need to test for available memory at all.
  int forced_headroom;

  // As the benchmarks are not good enough to calculate tiled-gpu vs untiled-cpu we have a parameter exposed
  // in the cldevice conf key to balance this
  float advantage;  
} dt_opencl_device_t;

struct dt_bilateral_cl_global_t;
struct dt_local_laplacian_cl_global_t;
struct dt_dwt_cl_global_t; // wavelet decompose
struct dt_heal_cl_global_t; // healing
struct dt_colorspaces_cl_global_t; // colorspaces transform
struct dt_guided_filter_cl_global_t;

/**
 * main struct, stored in darktable.opencl.
 * holds pointers to all
 */
typedef struct dt_opencl_t
{
  dt_pthread_mutex_t lock;
  int inited;
  int print_statistics;
  int enabled;
  int stopped;
  int num_devs;
  int error_count;
  int opencl_synchronization_timeout;
  dt_opencl_scheduling_profile_t scheduling_profile;
  uint32_t crc;
  int mandatory[5];
  int *dev_priority_image;
  int *dev_priority_preview;
  int *dev_priority_preview2;
  int *dev_priority_export;
  int *dev_priority_thumbnail;
  dt_opencl_device_t *dev;
  dt_dlopencl_t *dlocl;

  // we want the cpu benchmark to be available
  float cpubenchmark;
  // global kernels for blending operations.
  struct dt_blendop_cl_global_t *blendop;

  // global kernels for bilateral filtering, to be reused by a few plugins.
  struct dt_bilateral_cl_global_t *bilateral;

  // global kernels for gaussian filtering, to be reused by a few plugins.
  struct dt_gaussian_cl_global_t *gaussian;

  // global kernels for interpolation resampling.
  struct dt_interpolation_cl_global_t *interpolation;

  // global kernels for local laplacian filter.
  struct dt_local_laplacian_cl_global_t *local_laplacian;

  // global kernels for dwt filter.
  struct dt_dwt_cl_global_t *dwt;

  // global kernels for heal filter.
  struct dt_heal_cl_global_t *heal;

  // global kernels for colorspaces filter.
  struct dt_colorspaces_cl_global_t *colorspaces;

  // global kernels for guided filter.
  struct dt_guided_filter_cl_global_t *guided_filter;
} dt_opencl_t;

/** description of memory requirements of local buffer
  * local buffer size will be calculated as:
  * (xoffset + xfactor * x) * (yoffset + yfactor * y) * cellsize + overhead; */
typedef struct dt_opencl_local_buffer_t
{
  const int xoffset;
  const int xfactor;
  const int yoffset;
  const int yfactor;
  const size_t cellsize;
  const size_t overhead;
  int sizex;  // initial value and final values after optimization
  int sizey;  // initial value and final values after optimization
} dt_opencl_local_buffer_t;

/** internally calls dt_clGetDeviceInfo, and takes care of memory allocation
 * afterwards, *param_value will point to memory block of size at least *param_value
 * which needs to be free()'d manually */
int dt_opencl_get_device_info(dt_opencl_t *cl, cl_device_id device, cl_device_info param_name, void **param_value,
                              size_t *param_value_size);

/** inits the opencl subsystem. */
void dt_opencl_init(dt_opencl_t *cl, const gboolean exclude_opencl, const gboolean print_statistics);

/** cleans up the opencl subsystem. */
void dt_opencl_cleanup(dt_opencl_t *cl);

const char *cl_errstr(cl_int error);
/** both finish functions return TRUE in case of success */
/** cleans up command queue. */
int dt_opencl_finish(const int devid);
/** cleans up command queue if in synchron mode or while exporting */
int dt_opencl_finish_sync_pipe(const int devid, const int pipetype);

/** enqueues a synchronization point. */
int dt_opencl_enqueue_barrier(const int devid);

/** locks a device for your thread's exclusive use */
int dt_opencl_lock_device(const int pipetype);

/** done with your command queue. */
void dt_opencl_unlock_device(const int dev);

/** calculates md5sums for a list of CL include files. */
void dt_opencl_md5sum(const char **files, char **md5sums);

/** loads the given .cl file and returns a reference to an internal program. */
int dt_opencl_load_program(const int dev, const int prog, const char *filename, const char *binname,
                           const char *cachedir, char *md5sum, char **includemd5, int *loaded_cached);

/** builds the given program. */
int dt_opencl_build_program(const int dev, const int prog, const char *binname, const char *cachedir,
                            char *md5sum, int loaded_cached);

/** inits a kernel. returns the index or -1 if fail. */
int dt_opencl_create_kernel(const int program, const char *name);

/** releases kernel resources again. */
void dt_opencl_free_kernel(const int kernel);

/** return max size in sizes[3]. */
int dt_opencl_get_max_work_item_sizes(const int dev, size_t *sizes);

/** return max size per dimension in sizes[3] and max total size in workgroupsize */
int dt_opencl_get_work_group_limits(const int dev, size_t *sizes, size_t *workgroupsize,
                                    unsigned long *localmemsize);

/** return max workgroup size for a specific kernel */
int dt_opencl_get_kernel_work_group_size(const int dev, const int kernel, size_t *kernelworkgroupsize);

/** attach arg. */
int dt_opencl_set_kernel_arg(const int dev, const int kernel, const int num, const size_t size,
                             const void *arg);

/** launch kernel! */
int dt_opencl_enqueue_kernel_2d(const int dev, const int kernel, const size_t *sizes);

/** launch kernel with defined local size! */
int dt_opencl_enqueue_kernel_2d_with_local(const int dev, const int kernel, const size_t *sizes,
                                           const size_t *local);

/** check if opencl is inited */
int dt_opencl_is_inited(void);

/** check if opencl is enabled */
int dt_opencl_is_enabled(void);

/** disable opencl */
void dt_opencl_disable(void);

/** get OpenCL tuning mode flags */
int dt_opencl_get_tuning_mode(void);

/** runtime check for cl system running */
gboolean dt_opencl_running(void);

/** update enabled flag and profile with value from preferences */
void dt_opencl_update_settings(void);

/** HAVE_OPENCL mode only: copy and alloc buffers. */
int dt_opencl_copy_device_to_host(const int devid, void *host, void *device, const int width,
                                  const int height, const int bpp);

int dt_opencl_read_host_from_device(const int devid, void *host, void *device, const int width,
                                    const int height, const int bpp);

int dt_opencl_read_host_from_device_rowpitch(const int devid, void *host, void *device, const int width,
                                             const int height, const int rowpitch);

int dt_opencl_read_host_from_device_non_blocking(const int devid, void *host, void *device, const int width,
                                                 const int height, const int bpp);

int dt_opencl_read_host_from_device_rowpitch_non_blocking(const int devid, void *host, void *device,
                                                          const int width, const int height,
                                                          const int rowpitch);

int dt_opencl_read_host_from_device_raw(const int devid, void *host, void *device, const size_t *origin,
                                        const size_t *region, const int rowpitch, const int blocking);

int dt_opencl_write_host_to_device(const int devid, void *host, void *device, const int width,
                                   const int height, const int bpp);

int dt_opencl_write_host_to_device_rowpitch(const int devid, void *host, void *device, const int width,
                                            const int height, const int rowpitch);

int dt_opencl_write_host_to_device_non_blocking(const int devid, void *host, void *device, const int width,
                                                const int height, const int bpp);

int dt_opencl_write_host_to_device_rowpitch_non_blocking(const int devid, void *host, void *device,
                                                         const int width, const int height,
                                                         const int rowpitch);

int dt_opencl_write_host_to_device_raw(const int devid, void *host, void *device, const size_t *origin,
                                       const size_t *region, const int rowpitch, const int blocking);

void *dt_opencl_copy_host_to_device(const int devid, void *host, const int width, const int height,
                                    const int bpp);

void *dt_opencl_copy_host_to_device_rowpitch(const int devid, void *host, const int width, const int height,
                                             const int bpp, const int rowpitch);

void *dt_opencl_copy_host_to_device_constant(const int devid, const size_t size, void *host);

int dt_opencl_enqueue_copy_image(const int devid, cl_mem src, cl_mem dst, size_t *orig_src, size_t *orig_dst,
                                 size_t *region);

void *dt_opencl_alloc_device(const int devid, const int width, const int height, const int bpp);

void *dt_opencl_alloc_device_use_host_pointer(const int devid, const int width, const int height,
                                              const int bpp, const int rowpitch, void *host);

int dt_opencl_enqueue_copy_image_to_buffer(const int devid, cl_mem src_image, cl_mem dst_buffer,
                                           size_t *origin, size_t *region, size_t offset);

int dt_opencl_enqueue_copy_buffer_to_image(const int devid, cl_mem src_buffer, cl_mem dst_image,
                                           size_t offset, size_t *origin, size_t *region);

int dt_opencl_enqueue_copy_buffer_to_buffer(const int devid, cl_mem src_buffer, cl_mem dst_buffer,
                                            size_t srcoffset, size_t dstoffset, size_t size);

int dt_opencl_read_buffer_from_device(const int devid, void *host, void *device, const size_t offset,
                                      const size_t size, const int blocking);

int dt_opencl_write_buffer_to_device(const int devid, void *host, void *device, const size_t offset,
                                     const size_t size, const int blocking);

void *dt_opencl_alloc_device_buffer(const int devid, const size_t size);

void *dt_opencl_alloc_device_buffer_with_flags(const int devid, const size_t size, const int flags);

void dt_opencl_release_mem_object(cl_mem mem);

void *dt_opencl_map_buffer(const int devid, cl_mem buffer, const int blocking, const int flags, size_t offset,
                           size_t size);

int dt_opencl_unmap_mem_object(const int devid, cl_mem mem_object, void *mapped_ptr);

size_t dt_opencl_get_mem_object_size(cl_mem mem);

int dt_opencl_get_image_width(cl_mem mem);

int dt_opencl_get_image_height(cl_mem mem);

int dt_opencl_get_image_element_size(cl_mem mem);

int dt_opencl_get_mem_context_id(cl_mem mem);

void dt_opencl_memory_statistics(int devid, cl_mem mem, dt_opencl_memory_t action);

/** check if image size fit into limits given by OpenCL runtime */
gboolean dt_opencl_image_fits_device(const int devid, const size_t width, const size_t height, const unsigned bpp,
                                const float factor, const size_t overhead);
/** get available memory for the device */
cl_ulong dt_opencl_get_device_available(const int devid);

/** check tuning settings and available memory for the device */
void dt_opencl_check_tuning(const int devid);

/** get size of allocatable single buffer */
cl_ulong dt_opencl_get_device_memalloc(const int devid);

/** round size to a multiple of the value given in the device specifig config parameter for opencl_size_roundup */
int dt_opencl_dev_roundup_width(int size, const int devid);
int dt_opencl_dev_roundup_height(int size, const int devid);

/** get next free slot in eventlist and manage size of eventlist */
cl_event *dt_opencl_events_get_slot(const int devid, const char *tag);

/** reset eventlist to empty state */
void dt_opencl_events_reset(const int devid);

/** Wait for events in eventlist to terminate -> this is a blocking synchronization point
    Does not flush eventlist */
void dt_opencl_events_wait_for(const int devid);

/** Wait for events in eventlist to terminate, check for return status of events and
    report summary success info (CL_COMPLETE or last error code) */
cl_int dt_opencl_events_flush(const int devid, const int reset);

/** display OpenCL profiling information. If summary is not 0, try to generate summarized info for kernels */
void dt_opencl_events_profiling(const int devid, const int aggregated);

/** utility function to calculate optimal work group dimensions for a given kernel */
int dt_opencl_local_buffer_opt(const int devid, const int kernel, dt_opencl_local_buffer_t *factors);

/** utility functions handling device specific properties */
void dt_opencl_write_device_config(const int devid);
gboolean dt_opencl_read_device_config(const int devid);
int dt_opencl_avoid_atomics(const int devid);
int dt_opencl_micro_nap(const int devid);
gboolean dt_opencl_use_pinned_memory(const int devid);

#else
#include "control/conf.h"
#include <stdlib.h>
typedef struct dt_opencl_t
{
  int inited;
  int enabled;
  int stopped;
  int error_count;
} dt_opencl_t;
static inline void dt_opencl_init(dt_opencl_t *cl, const gboolean exclude_opencl, const gboolean print_statistics)
{
  cl->inited = 0;
  cl->enabled = 0;
  cl->stopped = 0;
  cl->error_count = 0;
  dt_conf_set_bool("opencl", FALSE);
  dt_print(DT_DEBUG_OPENCL, "[opencl_init] this version of darktable was built without opencl support\n");
}
static inline void dt_opencl_cleanup(dt_opencl_t *cl)
{
}
static inline gboolean dt_opencl_finish(const int devid)
{
  return -1;
}
static inline gboolean dt_opencl_finish_sync_pipe(const int devid, const int pipetype)
{
  return -1;
}
static inline int dt_opencl_enqueue_barrier(const int devid)
{
  return -1;
}
static inline int dt_opencl_lock_device(const int dev)
{
  return -1;
}
static inline void dt_opencl_unlock_device(const int dev)
{
}
static inline int dt_opencl_load_program(const int dev, const char *filename)
{
  return -1;
}
static inline int dt_opencl_build_program(const int dev, const int program)
{
  return -1;
}
static inline int dt_opencl_create_kernel(const int program, const char *name)
{
  return -1;
}
static inline void dt_opencl_free_kernel(const int kernel)
{
}
static inline int dt_opencl_get_max_work_item_sizes(const int dev, size_t *sizes)
{
  return -1;
}
static inline int dt_opencl_get_work_group_limits(const int dev, size_t *sizes, size_t *workgroupsize,
                                                  unsigned long *localmemsize)
{
  return -1;
}
static inline int dt_opencl_get_kernel_work_group_size(const int dev, const int kernel,
                                                       size_t *kernelworkgroupsize)
{
  return -1;
}
static inline int dt_opencl_set_kernel_arg(const int dev, const int kernel, const size_t size, const void *arg)
{
  return -1;
}
static inline int dt_opencl_enqueue_kernel_2d(const int dev, const int kernel, const size_t *sizes)
{
  return -1;
}
static inline int dt_opencl_enqueue_kernel_2d_with_local(const int dev, const int kernel, const size_t *sizes,
                                                         const size_t *local)
{
  return -1;
}
static inline int dt_opencl_is_inited(void)
{
  return 0;
}
static inline int dt_opencl_is_enabled(void)
{
  return 0;
}
static inline void dt_opencl_disable(void)
{
}
/** get OpenCL tuning mode flags */
static inline int dt_opencl_get_tuning_mode(void)
{
  return 0;
}
static inline gboolean dt_opencl_running(void)
{
  return FALSE;
}
static inline void dt_opencl_update_settings(void)
{
  return ;
}
static inline gboolean dt_opencl_image_fits_device(const int devid, const size_t width, const size_t height,
                                              const unsigned bpp, const float factor, const size_t overhead)
{
  return FALSE;
}
static inline size_t dt_opencl_get_device_available(const int devid)
{
  return 0;
}
static inline void dt_opencl_check_tuning(const int devid)
{
  return;
}
static inline size_t dt_opencl_get_device_memalloc(const int devid)
{
  return 0;
}
static inline void dt_opencl_release_mem_object(void *mem)
{
}
static inline void *dt_opencl_events_get_slot(const int devid, const char *tag)
{
  return NULL;
}
static inline void dt_opencl_events_reset(const int devid)
{
}
static inline void dt_opencl_events_wait_for(const int devid)
{
}
static inline int dt_opencl_events_flush(const int devid, const int reset)
{
  return 0;
}
static inline void dt_opencl_events_profiling(const int devid, const int aggregated)
{
}
#endif

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on

