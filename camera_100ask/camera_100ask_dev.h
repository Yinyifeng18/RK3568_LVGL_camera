#ifndef CAMERA_100ASK_DEV_H
#define CAMERA_100ASK_DEV_H


int camera_100ask_dev_init(void);

unsigned char *camera_100ask_dev_get_video_buf_cur(void);

void camera_100ask_dev_set_opt(unsigned char opt);

#endif /*CAMERA_100ASK_DEV_H*/
