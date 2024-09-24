#include "camera_100ask_ui.h"
#include "camera_100ask_dev.h"
#include <pthread.h>

#define PATH_FILE_NAME_LEN 256
#define BLINK_TIME         200 /*ms*/
#define BOOT_TIME          1500

typedef struct{
	uint8_t * name;            // 名称
} photo_file_t;

static lv_img_dsc_t * img_dsc;
static lv_obj_t * g_obj_blink;
unsigned char picture_data[720*1280*4] = {0};
unsigned char *temp_data;

extern pthread_mutex_t g_camera_100ask_mutex;
extern const unsigned char test_picture[2764800UL + 1];


static void lv_100ask_boot_animation(uint32_t boot_time);
static void camera_startup_timer(lv_timer_t * timer);
static void camera_work_timer(lv_timer_t * timer);
static void blink_timer(lv_timer_t * timer);
static void btn_capture_event_handler(lv_event_t * e);


void camera_100ask_ui_init(void)
{
    //printf("file: %s, line: %d, function: %s !\n", __FILE__, __LINE__, __FUNCTION__);

    camera_100ask_dev_init();

    // 开机动画
    lv_100ask_boot_animation(BOOT_TIME);

    // 创建全屏容器
    lv_obj_t * cont = lv_obj_create(lv_scr_act());
    lv_obj_set_style_radius(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_size(cont, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_y(cont, 0);
    printf("WEIGHT: %d, HEIGHT: %d\n", LV_HOR_RES, LV_VER_RES);
    // 让容器对象以淡入的方式显示，持续时间为BOOT_TIME
    lv_obj_fade_in(cont, 0, BOOT_TIME);

    // 分配图片内存
    img_dsc = lv_mem_alloc(sizeof(lv_img_dsc_t));
    lv_memset_00(img_dsc, sizeof(lv_img_dsc_t)); 

    // 创建图片 
    lv_obj_t * img = lv_img_create(cont);
    lv_img_set_antialias(img, true);
    lv_obj_center(img);   // 图片 剧中
    lv_timer_t * timer = lv_timer_create(camera_startup_timer, BOOT_TIME, img);
    lv_timer_set_repeat_count(timer, 1);  // 设置定时器只执行一次
    printf("===>lv_timer_create(camera_startup_timer, BOOT_TIME, img);\n");

    /*Blinking effect*/
    // 全屏的黑色透明对象
    g_obj_blink = lv_obj_create(cont);
    lv_obj_set_style_border_width(g_obj_blink, 0, 0);
    lv_obj_set_style_pad_all(g_obj_blink, 0, 0);
    lv_obj_set_style_radius(g_obj_blink, 0, 0);
    lv_obj_set_size(g_obj_blink, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(g_obj_blink, lv_color_hex(0x000000), 0);
    lv_obj_add_flag(g_obj_blink, LV_OBJ_FLAG_HIDDEN);

    /*btn_capture*/
    static lv_style_t style;
    lv_style_init(&style);

    lv_style_set_radius(&style, LV_RADIUS_CIRCLE);

    lv_style_set_bg_opa(&style, LV_OPA_100);
    lv_style_set_bg_color(&style, lv_color_hex(0xffffff));

    lv_style_set_border_opa(&style, LV_OPA_40);
    lv_style_set_border_width(&style, 2);
    lv_style_set_border_color(&style, lv_color_hex(0x000000));

    lv_style_set_outline_opa(&style, LV_OPA_COVER);
    lv_style_set_outline_color(&style, lv_color_hex(0x000000));

    lv_style_set_text_color(&style, lv_color_white());
    lv_style_set_pad_all(&style, 10);

    /*Init the pressed style*/
    static lv_style_t style_pr;
    lv_style_init(&style_pr);

    /*Ad a large outline when pressed*/
    lv_style_set_outline_width(&style_pr, 15);
    lv_style_set_outline_opa(&style_pr, LV_OPA_TRANSP);

    lv_style_set_translate_y(&style_pr, 5);
    //lv_style_set_shadow_ofs_y(&style_pr, 3);
    lv_style_set_bg_color(&style_pr, lv_color_hex(0xffffff));
    lv_style_set_bg_grad_color(&style_pr, lv_palette_main(LV_PALETTE_GREEN));

    /*Add a transition to the the outline*/
    // 边框效果
    static lv_style_transition_dsc_t trans;
    static lv_style_prop_t props[] = {LV_STYLE_OUTLINE_WIDTH, LV_STYLE_OUTLINE_OPA, 0};
    lv_style_transition_dsc_init(&trans, props, lv_anim_path_linear, 300, 0, NULL);
    lv_style_set_transition(&style_pr, &trans);

    lv_obj_t * cont_capture = lv_obj_create(cont);
    lv_obj_set_size(cont_capture, 100, 100);
    lv_obj_set_pos(cont_capture, 310, 1080);
    lv_obj_clear_flag(cont_capture, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(cont_capture, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(cont_capture, 0, 0);

    lv_obj_t * btn_capture = lv_btn_create(cont_capture);
    lv_obj_set_size(btn_capture, 75, 75);
    lv_obj_set_align(btn_capture, LV_ALIGN_CENTER);

    lv_obj_add_style(btn_capture, &style, 0);
    lv_obj_add_style(btn_capture, &style_pr, LV_STATE_PRESSED);

    lv_obj_add_event_cb(btn_capture, btn_capture_event_handler, LV_EVENT_ALL, NULL);

    /*camera setting*/
    lv_obj_t * btn_setting = lv_btn_create(cont);
    lv_obj_set_style_radius(btn_setting, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_size(btn_setting, 50, 50);
    lv_obj_set_pos(btn_setting, 160, 1105);

    lv_obj_t * label_setting = lv_label_create(btn_setting);
    lv_obj_set_style_text_font(label_setting, &lv_font_montserrat_28, 0);
    lv_label_set_text(label_setting, LV_SYMBOL_SETTINGS);
    lv_obj_set_align(label_setting, LV_ALIGN_CENTER);

    /* 去掉滑动调节亮度的 */

    /*Photo Browser*/
    lv_obj_t * btn_open_photo_browser = lv_btn_create(cont);
    lv_obj_set_style_radius(btn_open_photo_browser, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_size(btn_open_photo_browser, 50, 50);
    lv_obj_set_pos(btn_open_photo_browser, 510, 1105);

    lv_obj_t * label_photo_browser = lv_label_create(btn_open_photo_browser);
    lv_obj_set_style_text_font(label_photo_browser, &lv_font_montserrat_28, 0);
    lv_label_set_text(label_photo_browser, LV_SYMBOL_IMAGE);
    lv_obj_set_align(label_photo_browser, LV_ALIGN_CENTER);
    //printf("file: %s, line: %d, function: %s !\n", __FILE__, __LINE__, __FUNCTION__);
}



static void lv_100ask_boot_animation(uint32_t boot_time)
{
    //printf("file: %s, line: %d, function: %s !\n", __FILE__, __LINE__, __FUNCTION__);

    LV_IMG_DECLARE(img_lv_100ask_demo_logo);
    lv_obj_t * logo = lv_img_create(lv_scr_act());
    lv_img_set_src(logo, &img_lv_100ask_demo_logo);
    lv_obj_align(logo, LV_ALIGN_CENTER, 0, 0);

    /*Animate in the content after the intro time*/
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_path_cb(&a, lv_anim_path_bounce);
    lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
    lv_anim_set_var(&a, logo);
    lv_anim_set_time(&a, boot_time);
    lv_anim_set_delay(&a, 0);
    lv_anim_set_values(&a, 1, LV_IMG_ZOOM_NONE);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t) lv_img_set_zoom);
	lv_anim_set_ready_cb(&a, lv_obj_del_anim_ready_cb);
    lv_anim_start(&a);

    /* Create an intro from a label */
    lv_obj_t * title = lv_label_create(lv_scr_act());
    //lv_label_set_text(title, "100ASK LVGL DEMO\nhttps://www.100ask.net\nhttp:/lvgl.100ask.net");
	lv_label_set_text(title, "100ASK LVGL DEMO");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, LV_STATE_DEFAULT); // Please enable LV_FONT_MONTSERRAT_22 in lv_conf.h
    lv_obj_set_style_text_line_space(title, 8, LV_STATE_DEFAULT);
    lv_obj_align_to(title, logo, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);

    lv_obj_fade_out(title, 0, boot_time);
    lv_obj_fade_out(logo, 0, boot_time);
}

static void camera_startup_timer(lv_timer_t * timer)
{
    lv_obj_t * img = (lv_obj_t *)timer->user_data;
    lv_timer_create(camera_work_timer, 50, img);
    //printf("file: %s, line: %d, function: %s !\n", __FILE__, __LINE__, __FUNCTION__);
}



static void camera_work_timer(lv_timer_t * timer)
{
    int i, j = 0;
    //printf("file: %s, line: %d, function: %s !\n", __FILE__, __LINE__, __FUNCTION__);
    /*Use the user_data*/
    lv_obj_t * img = (lv_obj_t *)timer->user_data;
    
    /*Do something with LVGL*/
    pthread_mutex_lock(&g_camera_100ask_mutex);

    // 获取视频数据
    #if 1
    unsigned char *buffer  = camera_100ask_dev_get_video_buf_cur();
    // 用于测试，实现显示用视频数据
    j = 0;
    for(i = 0; i < 3*720*1280; i += 3)
    {
        picture_data[j] = buffer[i];
        j++;
        picture_data[j] = buffer[i+1];
        j++;
        picture_data[j] = buffer[i+2];
        j++;
        picture_data[j] = 0xff;
        j++;
    }
    #else 
    // 用于测试，实现显示用视频数据
    j = 0;
    for(i = 0; i < 3*720*1280; i += 3)
    {
        picture_data[j] = 0xff;
        j++;
        picture_data[j] = test_picture[i];
        j++;
        picture_data[j] = test_picture[i+1];
        j++;
        picture_data[j] = test_picture[i+2];
        j++;
    }
    #endif

    img_dsc->data = &picture_data[0];

    img_dsc->data_size = 4*720*1280;
   
    img_dsc->header.w = 720;
    img_dsc->header.h = 1280;
    img_dsc->header.cf = LV_IMG_CF_TRUE_COLOR;  // 真彩色 RGB888
    lv_img_set_src(img, img_dsc);

    pthread_mutex_unlock(&g_camera_100ask_mutex); 
}


static void blink_timer(lv_timer_t * timer)
{
    //printf("file: %s, line: %d, function: %s !\n", __FILE__, __LINE__, __FUNCTION__);
    lv_obj_add_flag(g_obj_blink, LV_OBJ_FLAG_HIDDEN);
}

static void btn_capture_event_handler(lv_event_t * e)
{
    //printf("file: %s, line: %d, function: %s !\n", __FILE__, __LINE__, __FUNCTION__);
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * btn_capture = lv_event_get_target(e);

    if(code == LV_EVENT_CLICKED) 
    {
        camera_100ask_dev_set_opt(1);

        lv_obj_clear_flag(g_obj_blink, LV_OBJ_FLAG_HIDDEN);

        lv_timer_t * timer = lv_timer_create(blink_timer, BLINK_TIME, NULL);
        lv_timer_set_repeat_count(timer, 1);
    }
}



