#include "camera_100ask_dev.h"

#include <time.h>
#include <string.h>
#include <pthread.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <asm/types.h>
#include <linux/videodev2.h>
#include <linux/v4l2-subdev.h>

#include "convert_to_bmp_file.h"

#define FMT_NUM_PLANES 1

pthread_mutex_t g_camera_100ask_mutex;

unsigned char rgb24[720*1280*3] = {0};
unsigned char argb32[720*1280*4] = {0};

struct buffer
{
    void *start;
    size_t length;
};


 
typedef struct lcd_color
{
    unsigned char bule;
    unsigned char green;
    unsigned char red;
    unsigned char alpha;
} lcd_color;
 


struct v4l2_dev
{
    int fd;
    int sub_fd;
    const char *path;
    const char *name;
    const char *subdev_path;
    const char *out_type;
    enum v4l2_buf_type buf_type;
    int format;
    int width;
    int height;
    unsigned int req_count;
    enum v4l2_memory memory_type;
    struct buffer *buffers;
    unsigned long int timestamp;
    int data_len;
    unsigned char *out_data;
};



struct v4l2_dev mx335 = {
    .fd = -1,
    .sub_fd = -1,
    .path = "/dev/video0",
    .name = "MX335",
    .subdev_path = "/dev/v4l-subdev0",
    .out_type = "nv12",
    .buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
    .format = V4L2_PIX_FMT_NV12,
    .width = 720,
    .height = 1280,
    .req_count = 4,
    .memory_type = V4L2_MEMORY_MMAP,
    .buffers = NULL,
    .timestamp = 0,
    .data_len = 0,
    .out_data = NULL,
};

struct v4l2_dev *camdev = &mx335;

static unsigned char  g_camera_opt = 0;

static long int crv_tab[256];
static long int cbu_tab[256];
static long int cgu_tab[256];
static long int cgv_tab[256];
static long int tab_76309[256];
static unsigned char clp[1024]; // for clip in CCIR601

static void init_yuv420p_table(void); 
static void nv12_to_rgb24(unsigned char *yuvbuffer, unsigned char *rga_buffer,
                   int width, int height);
static void open_device(struct v4l2_dev *dev);
static void get_capabilities(struct v4l2_dev *dev);
static void set_fmt(struct v4l2_dev *dev);
static void require_buf(struct v4l2_dev *dev);
static void alloc_buf(struct v4l2_dev *dev);
static void queue_buf(struct v4l2_dev *dev);
static void set_fps(struct v4l2_dev *dev, unsigned int fps);
static void stream_on(struct v4l2_dev *dev);
static void save_picture(const char *filename, unsigned char *file_data, unsigned int len, int is_overwrite);
static void stream_off(struct v4l2_dev *dev);
static void exit_failure(struct v4l2_dev *dev);
static void close_device(struct v4l2_dev *dev);


static void *thread_camera_work(void *args);


int camera_100ask_dev_init(void)
{
    //printf("file: %s, line: %d, function: %s !\n", __FILE__, __LINE__, __FUNCTION__);
    pthread_mutex_init(&g_camera_100ask_mutex, NULL);

    /* 创建线程 */
    pthread_t thread;
    pthread_create(&thread, NULL, thread_camera_work, NULL);

}



static void init_yuv420p_table(void) 
{
  long int crv, cbu, cgu, cgv;
  int i, ind;
  static int init = 0;
  //printf("file: %s, line: %d, function: %s !\n", __FILE__, __LINE__, __FUNCTION__);
  if (init == 1)
    return;
 
  crv = 104597;
  cbu = 132201; /* fra matrise i global.h */
  cgu = 25675;
  cgv = 53279;
 
  for (i = 0; i < 256; i++) {
    crv_tab[i] = (i - 128) * crv;
    cbu_tab[i] = (i - 128) * cbu;
    cgu_tab[i] = (i - 128) * cgu;
    cgv_tab[i] = (i - 128) * cgv;
    tab_76309[i] = 76309 * (i - 16);
  }
 
  for (i = 0; i < 384; i++)
    clp[i] = 0;
  ind = 384;
  for (i = 0; i < 256; i++)
    clp[ind++] = i;
  ind = 640;
  for (i = 0; i < 384; i++)
    clp[ind++] = 255;
 
  init = 1;
}
 
 
/*!
 * \fn     nv12_to_rgb24
 * \brief  nv12转Rgb24
 *          
 * \param  [in] unsigned char *yuvbuffer    #
 * \param  [in] unsigned char *rga_buffer   #
 * \param  [in] int width                   #
 * \param  [in] int height                  #
 * 
 * \retval void
 */
static void nv12_to_rgb24(unsigned char *yuvbuffer, unsigned char *rga_buffer,
                   int width, int height) 
{
  //printf("file: %s, line: %d, function: %s !\n", __FILE__, __LINE__, __FUNCTION__);
  int y1, y2, u, v;
  unsigned char *py1, *py2;
  int i, j, c1, c2, c3, c4;
  unsigned char *d1, *d2;
  unsigned char *src_u;
 
  src_u = yuvbuffer + width * height; // u
 
  py1 = yuvbuffer; // y
  py2 = py1 + width;
  d1 = rga_buffer;
  d2 = d1 + 3 * width;
 
  init_yuv420p_table();
 
  for (j = 0; j < height; j += 2) {
    for (i = 0; i < width; i += 2) {
      u = *src_u++;
      v = *src_u++; // v immediately follows u, in the next position of u
 
      c4 = crv_tab[v];
      c2 = cgu_tab[u];
      c3 = cgv_tab[v];
      c1 = cbu_tab[u];      
 
      // up-left
      y1 = tab_76309[*py1++];
      *d1++ = clp[384 + ((y1 + c1) >> 16)];
      *d1++ = clp[384 + ((y1 - c2 - c3) >> 16)];
      *d1++ = clp[384 + ((y1 + c4) >> 16)];
      
      // down-left
      y2 = tab_76309[*py2++];
      *d2++ = clp[384 + ((y2 + c1) >> 16)];
      *d2++ = clp[384 + ((y2 - c2 - c3) >> 16)];
      *d2++ = clp[384 + ((y2 + c4) >> 16)];
      
 
      // up-right
      y1 = tab_76309[*py1++];
      *d1++ = clp[384 + ((y1 + c1) >> 16)];
      *d1++ = clp[384 + ((y1 - c2 - c3) >> 16)];
      *d1++ = clp[384 + ((y1 + c4) >> 16)];
      // down-right
      y2 = tab_76309[*py2++];
      *d2++ = clp[384 + ((y2 + c1) >> 16)];
      *d2++ = clp[384 + ((y2 - c2 - c3) >> 16)];
      *d2++ = clp[384 + ((y2 + c4) >> 16)];
      
    }
    d1 += 3 * width;
    d2 += 3 * width;
    py1 += width;
    py2 += width;
  }
}


static void open_device(struct v4l2_dev *dev)
{
    //printf("file: %s, line: %d, function: %s !\n", __FILE__, __LINE__, __FUNCTION__);
    dev->fd = open(dev->path, O_RDWR | O_CLOEXEC, 0);
    if (dev->fd < 0) {
        printf("Cannot open %s\n\n", dev->path);
        exit_failure(dev);
    }
    printf("Open %s succeed - %d\n\n", dev->path, dev->fd);
 
    dev->sub_fd = open(dev->subdev_path, O_RDWR|O_CLOEXEC, 0);
    if (dev->sub_fd < 0) {
        printf("Cannot open %s\n\n", dev->subdev_path);
        exit_failure(dev);
    }
    printf("Open %s succeed\n\n", dev->subdev_path);
    return;
}
 
static void get_capabilities(struct v4l2_dev *dev)
{
    //printf("file: %s, line: %d, function: %s !\n", __FILE__, __LINE__, __FUNCTION__);
    struct v4l2_capability cap;
    if (ioctl(dev->fd, VIDIOC_QUERYCAP, &cap) < 0) {
        printf("VIDIOC_QUERYCAP failed\n");
        return;
    }
    printf("------- VIDIOC_QUERYCAP ----\n");
    printf("  driver: %s\n", cap.driver);
    printf("  card: %s\n", cap.card);
    printf("  bus_info: %s\n", cap.bus_info);
    printf("  version: %d.%d.%d\n",
           (cap.version >> 16) & 0xff,
           (cap.version >> 8) & 0xff,
           (cap.version & 0xff));
    printf("  capabilities: %08X\n", cap.capabilities);
 
    if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)
        printf("        Video Capture\n");
    if (cap.capabilities & V4L2_CAP_VIDEO_OUTPUT)
        printf("        Video Output\n");
    if (cap.capabilities & V4L2_CAP_VIDEO_OVERLAY)
        printf("        Video Overly\n");
    if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE)
        printf("        Video Capture Mplane\n");
    if (cap.capabilities & V4L2_CAP_VIDEO_OUTPUT_MPLANE)
        printf("        Video Output Mplane\n");
    if (cap.capabilities & V4L2_CAP_READWRITE)
        printf("        Read / Write\n");
    if (cap.capabilities & V4L2_CAP_STREAMING)
        printf("        Streaming\n");
    printf("\n");
    return;
}
 
 
 
static void set_fmt(struct v4l2_dev *dev)
{
    //printf("file: %s, line: %d, function: %s !\n", __FILE__, __LINE__, __FUNCTION__);
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = dev->buf_type;
    fmt.fmt.pix.pixelformat = dev->format;
    fmt.fmt.pix.width = dev->width;
    fmt.fmt.pix.height = dev->height;
    if (ioctl(dev->fd, VIDIOC_S_FMT, &fmt) < 0) {
        printf("VIDIOC_S_FMT failed - [%d]!\n", errno);
        exit_failure(dev);
    }
    printf("VIDIOC_S_FMT succeed!\n");
    dev->data_len = fmt.fmt.pix.sizeimage;
    printf("width %d, height %d, size %d, bytesperline %d, format %c%c%c%c\n\n",
           fmt.fmt.pix.width, fmt.fmt.pix.height, dev->data_len,
           fmt.fmt.pix.bytesperline,
           fmt.fmt.pix.pixelformat & 0xFF,
           (fmt.fmt.pix.pixelformat >> 8) & 0xFF,
           (fmt.fmt.pix.pixelformat >> 16) & 0xFF,
           (fmt.fmt.pix.pixelformat >> 24) & 0xFF);
    return;
}
 
 
 
static void require_buf(struct v4l2_dev *dev)
{
    //printf("file: %s, line: %d, function: %s !\n", __FILE__, __LINE__, __FUNCTION__);
    // 申请缓冲区
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = dev->req_count;
    req.type = dev->buf_type;
    req.memory = dev->memory_type;
    if (ioctl(dev->fd, VIDIOC_REQBUFS, &req) == -1) {
        printf("VIDIOC_REQBUFS failed!\n\n");
        exit_failure(dev);
    }
    if (dev->req_count != req.count) {
        printf("!!! req count = %d\n", req.count);
        dev->req_count = req.count;
    }
    printf("VIDIOC_REQBUFS succeed!\n\n");
    return;
}
 
static void alloc_buf(struct v4l2_dev *dev)
{
    //printf("file: %s, line: %d, function: %s !\n", __FILE__, __LINE__, __FUNCTION__);
    dev->buffers = (struct buffer *)calloc(dev->req_count, sizeof(*(dev->buffers)));
    for (unsigned int i = 0; i < dev->req_count; ++i) {
        struct v4l2_buffer buf;
        struct v4l2_plane planes[FMT_NUM_PLANES];
        memset(&buf, 0, sizeof(buf));
        memset(&planes, 0, sizeof(planes));
        buf.type = dev->buf_type;
        buf.memory = dev->memory_type;
        buf.index = i;
 
        if (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == dev->buf_type) {
            buf.m.planes = planes;
            buf.length = FMT_NUM_PLANES;
        }
 
        if (ioctl(dev->fd, VIDIOC_QUERYBUF, &buf) == -1) {
            printf("VIDIOC_QUERYBUF failed!\n\n");
            exit_failure(dev);
        }
        if (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == dev->buf_type) {
            dev->buffers[i].length = buf.m.planes[0].length;
            dev->buffers[i].start =
                mmap(NULL /* start anywhere */,
                     buf.m.planes[0].length,
                     PROT_READ | PROT_WRITE /* required */,
                     MAP_SHARED /* recommended */,
                     dev->fd, buf.m.planes[0].m.mem_offset);
        } else {
            dev->buffers[i].length = buf.length;
            dev->buffers[i].start = mmap(NULL,
                                         buf.length,
                                         PROT_READ | PROT_WRITE,
                                         MAP_SHARED,
                                         dev->fd,
                                         buf.m.offset);
        }
 
        if (dev->buffers[i].start == MAP_FAILED) {
            printf("Memory map failed!\n\n");
            exit_failure(dev);
        }
    }
    printf("Memory map succeed!\n\n");
    return;
}
 
static void queue_buf(struct v4l2_dev *dev)
{
    //printf("file: %s, line: %d, function: %s !\n", __FILE__, __LINE__, __FUNCTION__);
    for (unsigned int i = 0; i < dev->req_count; ++i) {
        struct v4l2_buffer buf;
        struct v4l2_plane planes[FMT_NUM_PLANES];
        memset(&buf, 0, sizeof(buf));
        memset(&planes, 0, sizeof(planes));
        buf.type = dev->buf_type;
        buf.memory = dev->memory_type;
        buf.index = i;
 
        if (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == dev->buf_type) {
            buf.m.planes = planes;
            buf.length = FMT_NUM_PLANES;
        }
 
        if (ioctl(dev->fd, VIDIOC_QBUF, &buf) < 0) {
            printf("VIDIOC_QBUF failed!\n\n");
            exit_failure(dev);
        }
    }
    printf("VIDIOC_QBUF succeed!\n\n");
    return;
}


 
 
static void set_fps(struct v4l2_dev *dev, unsigned int fps)
{
    //printf("file: %s, line: %d, function: %s !\n", __FILE__, __LINE__, __FUNCTION__);
	int ret;
	struct v4l2_subdev_frame_interval frame_int;
 
    if (fps == 0) return;
 
	memset(&frame_int, 0x00, sizeof(frame_int));
 
	frame_int.interval.numerator = 10000;
	frame_int.interval.denominator = fps * 10000;	
 
	ret = ioctl(dev->sub_fd, VIDIOC_SUBDEV_S_FRAME_INTERVAL, &frame_int);
	if (ret < 0) {
		printf("VIDIOC_SUBDEV_S_FRAME_INTERVAL error\n");
        goto set_fps_err;
	}
    printf("VIDIOC_SUBDEV_S_FRAME_INTERVAL [%u fps] OK\n", fps);
set_fps_err:
    return;
}

 
static void stream_on(struct v4l2_dev *dev)
{
    //printf("file: %s, line: %d, function: %s !\n", __FILE__, __LINE__, __FUNCTION__);
    enum v4l2_buf_type type = dev->buf_type;
    if (ioctl(dev->fd, VIDIOC_STREAMON, &type) == -1) {
        printf("VIDIOC_STREAMON failed!\n\n");
        exit_failure(dev);
    }
    printf("VIDIOC_STREAMON succeed!\n\n");
    return;
}



void get_frame(struct v4l2_dev *dev, int skip_frame)
{
    //printf("file: %s, line: %d, function: %s !\n", __FILE__, __LINE__, __FUNCTION__);
    struct v4l2_buffer buf;
    struct v4l2_plane planes[FMT_NUM_PLANES];
    for (int i = 0; i <= skip_frame; ++i) {
        memset(&buf, 0, sizeof(buf));
        buf.type = dev->buf_type;
        buf.memory = dev->memory_type;
 
        if (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == dev->buf_type) {
            buf.m.planes = planes;
            buf.length = FMT_NUM_PLANES;
        }
 
        if (ioctl(dev->fd, VIDIOC_DQBUF, &buf) == -1) {
            printf("VIDIOC_DQBUF failed!\n\n");
            exit_failure(dev);
        }
 
        dev->out_data = (unsigned char *)dev->buffers[buf.index].start;
        dev->timestamp = buf.timestamp.tv_sec * 1000000 + buf.timestamp.tv_usec;
        //printf("image: sequence = %d, timestamp = %lu\n", buf.sequence, dev->timestamp);
 
        if (ioctl(dev->fd, VIDIOC_QBUF, &buf) == -1) {
            printf("VIDIOC_QBUF failed!\n");
            exit_failure(dev);
        }
    }
    return;
}


static void save_picture(const char *filename, unsigned char *file_data, unsigned int len, int is_overwrite)
{
    //printf("file: %s, line: %d, function: %s !\n", __FILE__, __LINE__, __FUNCTION__);
    FILE *fp;
    if (is_overwrite)
        fp = fopen(filename, "wb");
    else
        fp = fopen(filename, "ab");
    if (fp < 0) {
        printf("Open frame data file failed\n\n");
        return;
    }
    if (fwrite(file_data, 1, len, fp) < len) {
        printf("Out of memory!\n");
        return;
    }
    fflush(fp);
    fclose(fp);
   
    return;
}


static void stream_off(struct v4l2_dev *dev)
{
    //printf("file: %s, line: %d, function: %s !\n", __FILE__, __LINE__, __FUNCTION__);
    enum v4l2_buf_type type;
    type = dev->buf_type;
    if (ioctl(dev->fd, VIDIOC_STREAMOFF, &type) == -1) {
        printf("VIDIOC_STREAMOFF failed!\n\n");
        exit_failure(dev);
    }
    printf("VIDIOC_STREAMOFF succeed!\n\n");
    return;
}


static void exit_failure(struct v4l2_dev *dev)
{
    //printf("file: %s, line: %d, function: %s !\n", __FILE__, __LINE__, __FUNCTION__);
    close_device(dev);
    exit(EXIT_FAILURE);
}


 
 
static void close_device(struct v4l2_dev *dev)
{
    //printf("file: %s, line: %d, function: %s !\n", __FILE__, __LINE__, __FUNCTION__);
    if (dev->buffers) {
        for (unsigned int i = 0; i < dev->req_count; ++i) {
            if (dev->buffers[i].start) {
                munmap(dev->buffers[i].start, dev->buffers[i].length);
            }
        }
        free(dev->buffers);
    }
    if (-1 != dev->fd) {
        close(dev->fd);
    }
    if (-1 != dev->sub_fd) {
        close(dev->sub_fd);
    }
    return;
}



static void *thread_camera_work(void *args)
{
    //printf("file: %s, line: %d, function: %s !\n", __FILE__, __LINE__, __FUNCTION__);
    unsigned int fps = 30;
    char name[40] = {0};
    time_t timep;
    struct tm *p;
    char time_buffer [64];
 
    open_device(camdev);          // 1 打开摄像头设备
    get_capabilities(camdev);
    set_fmt(camdev);              // 2 设置出图格式
    require_buf(camdev);          // 3 申请缓冲区
    alloc_buf(camdev);            // 4 内存映射
    queue_buf(camdev);            // 5 将缓存帧加入队列
    set_fps(camdev, fps);
 
    stream_on(camdev);            // 6 开启视频流

    while(1)
    {
        pthread_mutex_lock(&g_camera_100ask_mutex);
    
        /* 获取视频数据 */
        get_frame(camdev, 2); // 2帧取一帧数据

        /* 保存成文件 */
        snprintf(name, sizeof(name), "./%s.%s", camdev->name, camdev->out_type);
	    save_picture(name, camdev->out_data, camdev->data_len, 1);

        /* 数据转换 NV12 转RGB24 */
        nv12_to_rgb24(camdev->out_data, rgb24, 720, 1280);
        

        pthread_mutex_unlock(&g_camera_100ask_mutex);

        switch (g_camera_opt)
        {
            case 0:
                g_camera_opt = 0;
                break;
            case 1:
                //printf("file: %s, line: %d, function: %s !\n", __FILE__, __LINE__, __FUNCTION__);
                time(&timep);
                p=gmtime(&timep);
                strftime (time_buffer, sizeof(time_buffer),"picture-%Y%m%d-%H%M%S.bmp",p);
                printf("photos name: %s\n", time_buffer);

                CvtRgb2BMPFileFrmFrameBuffer(&rgb24[0], 720, 1280, 24, time_buffer);

                system("ls");
                
                g_camera_opt = 0;
                break;
            default:
                g_camera_opt = 0;
                break;    
        }

        usleep(1000*10);
    }

    stream_off(camdev);   // 8 关闭视频流
    close_device(camdev); // 9 释放内存关闭文件

    return NULL;
}


unsigned char *camera_100ask_dev_get_video_buf_cur(void)
{
    return &rgb24[0];
}

void camera_100ask_dev_set_opt(unsigned char opt)
{
    g_camera_opt = opt;
    //printf("file: %s, line: %d, function: %s !\n", __FILE__, __LINE__, __FUNCTION__);
}