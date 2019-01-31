/****************************************************************************
*   RKNN Runtime Test
****************************************************************************/

/*-------------------------------------------
                Includes
-------------------------------------------*/
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <minigui/common.h>
#include <minigui/minigui.h>
#include <minigui/gdi.h>
#include <minigui/window.h>
#include <minigui/ctrl/static.h>

#include "rknn_msg.h"
#include "ui_res.h"

#if ENABLE_SSD
#include "ssd.h"
#include "ssd_ui.h"
#endif

#if ENABLE_JOINT
#include "joint.h"
#include "joint_ui.h"
#endif

#define TEXT_SIZE_MAIN    18
#define _ID_TIMER      106

static HWND g_main_hwnd;
static DWORD g_bkcolor;
static PLOGFONT g_main_font = NULL;

typedef int (*paint_callback_t)(HWND hwnd);
paint_callback_t g_paint_callback_func;
typedef int (*rknn_callback_t)(void *arg);

int g_post_flag;
int g_run_flag;
pthread_t g_run_tid;
pthread_t g_post_tid;

int rknn_reg_paint_callback(paint_callback_t func)
{
    g_paint_callback_func = func;
}

paint_callback_t rknn_get_paint_callback()
{
    return g_paint_callback_func;
}

void *rknn_run_pth(void *arg)
{
    rknn_callback_t run_func = (rknn_callback_t)arg;
    if (run_func) {
        g_run_flag = 1;
        // If the flag be set to 0, phread need end self.
        run_func((void *)&g_run_flag);
    }
    pthread_exit(0);
}

int rknn_run_pth_create(rknn_callback_t func) {
    if (pthread_create(&g_run_tid, NULL,
                     rknn_run_pth, (void *)func)) {
        printf("creae pthread fail\n");
        return -1;
    }
    return 0;
}

int rknn_run_pth_destory()
{
    if (g_run_tid) {
        g_run_flag = 0;
        pthread_join(g_run_tid, NULL);
        g_run_tid = 0;
    }
}

void *rknn_post_pth(void *arg)
{
    rknn_callback_t post_func = (rknn_callback_t)arg;
    if (post_func) {
        g_post_flag = 1;
        // If the flag be set to 0, phread need end self.
        post_func((void *)&g_post_flag);
    }
    pthread_exit(0);
}

int rknn_post_pth_create(rknn_callback_t func)
{
    if (pthread_create(&g_post_tid, NULL,
                       rknn_post_pth, (void *)func)) {
        printf("creae pthread fail\n");
        return -1;
    }
    return 0;
}

int rknn_post_pth_destory()
{
    if (g_post_tid) {
        g_post_flag = 0;
        pthread_join(g_post_tid, NULL);
        g_post_tid = 0;
    }
}

int rknn_child_win_init(HWND hwnd)
{
    int ret;
#if ENABLE_SSD
    rknn_reg_paint_callback(ssd_paint_object);
    ret = ssd_ui_init(hwnd);
    assert(!ret);
#endif

#if ENABLE_JOINT
    rknn_reg_paint_callback(joint_paint_object);
    ret = joint_ui_init(hwnd);
    assert(!ret);
#endif
}

int rknn_child_win_deinit(HWND hwnd)
{
#if ENABLE_SSD
    ssd_ui_deinit(hwnd);
#endif
#if ENABLE_JOINT
    joint_ui_deinit(hwnd);
#endif
}

int rknn_demo_init()
{
    rknn_callback_t post;
    rknn_callback_t run;
#if ENABLE_SSD
    ssd_init(0);
    post = ssd_post;
    run = ssd_run;
#endif
#if ENABLE_JOINT
    joint_init(0);
    post = joint_post;
    run = joint_run;
#endif
    rknn_post_pth_create(post);
    rknn_run_pth_create(run);
}

int rknn_demo_deinit()
{
#if ENABLE_SSD
    ssd_deinit();
#endif
#if ENABLE_JOINT
    joint_deinit();
#endif
    rknn_run_pth_destory();
    rknn_post_pth_destory();
}

static LRESULT rknn_win_proc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param)
{
    switch (message) {
    case MSG_CREATE:
        SetTimer(hwnd, _ID_TIMER, 1);
        break;
    case MSG_TIMER:
        break;
    case MSG_PAINT:
        if (g_paint_callback_func)
            g_paint_callback_func(hwnd);
        break;
    case MSG_KEYDOWN :
        switch (w_param) {
        case SCANCODE_ENTER:
            break;
        case SCANCODE_CURSORBLOCKLEFT:
            break;
        }
    }
    return DefaultMainWinProc(hwnd, message, w_param, l_param);
}

int rknn_ui_show()
{
    MSG msg;
    HDC sndHdc;
    MAINWINCREATE create_info;

    if (loadres()) {
        printf("loadres fail\n");
        return -1;
    }

    memset(&create_info, 0, sizeof(MAINWINCREATE));
    create_info.dwStyle = WS_VISIBLE;
    create_info.dwExStyle = WS_EX_NONE | WS_EX_AUTOSECONDARYDC;
    create_info.spCaption = "camera";
    //create_info.hCursor = GetSystemCursor(0);
    create_info.hIcon = 0;
    create_info.MainWindowProc = rknn_win_proc;
    create_info.lx = 0;
    create_info.ty = 0;
    create_info.rx = g_rcScr.right;
    create_info.by = g_rcScr.bottom;
    create_info.dwAddData = 0;
    create_info.hHosting = HWND_DESKTOP;
    //  create_info.language = 0; //en

    g_main_hwnd = CreateMainWindow(&create_info);
    if (g_main_hwnd == HWND_INVALID)
        return -1;
    g_main_font = CreateLogFont(FONT_TYPE_NAME_SCALE_TTF,
                                "ubuntuMono", "ISO8859-1",
                                FONT_WEIGHT_REGULAR, FONT_SLANT_ROMAN,
                                FONT_FLIP_NIL, FONT_OTHER_NIL,
                                FONT_UNDERLINE_NONE, FONT_STRUCKOUT_NONE,
                                TEXT_SIZE_MAIN, 0);
    SetWindowFont(g_main_hwnd, g_main_font);

    g_bkcolor = GetWindowElementPixel(g_main_hwnd, WE_BGC_DESKTOP);
    SetWindowBkColor(g_main_hwnd, g_bkcolor);
    sndHdc = GetSecondaryDC((HWND)g_main_hwnd);
    SetMemDCAlpha(sndHdc, MEMDC_FLAG_SWSURFACE, 0);
    ShowWindow(g_main_hwnd, SW_SHOWNORMAL);
    // Init child window
    rknn_child_win_init(g_main_hwnd);

    while (GetMessage(&msg, g_main_hwnd)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    rknn_child_win_deinit(g_main_hwnd);

    DestroyLogFont(g_main_font);
    DestroyMainWindow(g_main_hwnd);
    MainWindowThreadCleanup(g_main_hwnd);
    unloadres();
    return 0;
}

int MiniGUIMain(int argc, const char* argv[])
{
    rknn_demo_init();
    rknn_ui_show();
    rknn_demo_deinit();
}
